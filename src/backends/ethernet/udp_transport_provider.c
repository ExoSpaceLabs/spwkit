// SPDX-License-Identifier: Apache-2.0

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "backends/ethernet/udp_transport_provider.h"
#include "backends/ethernet/vspw_tp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef struct spw_udp_deadline {
    bool infinite;
    uint64_t end_us;
} spw_udp_deadline_t;

static uint64_t now_us(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0u;
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000);
}

static spw_udp_deadline_t deadline_make(spw_timeout_us_t timeout_us) {
    spw_udp_deadline_t deadline;
    deadline.infinite = timeout_us == SPW_TIMEOUT_INFINITE;
    if (deadline.infinite) {
        deadline.end_us = UINT64_MAX;
    } else {
        const uint64_t now = now_us();
        deadline.end_us = UINT64_MAX - now < timeout_us
                              ? UINT64_MAX
                              : now + timeout_us;
    }
    return deadline;
}

static spw_timeout_us_t deadline_remaining(const spw_udp_deadline_t* deadline) {
    uint64_t now;
    if (deadline->infinite) {
        return SPW_TIMEOUT_INFINITE;
    }
    now = now_us();
    return now >= deadline->end_us ? SPW_TIMEOUT_IMMEDIATE
                                   : deadline->end_us - now;
}

static bool deadline_expired(const spw_udp_deadline_t* deadline) {
    return !deadline->infinite && now_us() >= deadline->end_us;
}

static int timeout_ms(spw_timeout_us_t timeout_us) {
    uint64_t rounded;
    if (timeout_us == SPW_TIMEOUT_INFINITE) {
        return -1;
    }
    rounded = (timeout_us + 999u) / 1000u;
    return rounded > (uint64_t)INT_MAX ? INT_MAX : (int)rounded;
}

static spw_result_t wait_socket(int fd,
                                short events,
                                spw_timeout_us_t timeout_us) {
    struct pollfd descriptor;
    int ready;
    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.fd = fd;
    descriptor.events = events;
    do {
        ready = poll(&descriptor, 1, timeout_ms(timeout_us));
    } while (ready < 0 && errno == EINTR);
    if (ready == 0) {
        return SPW_ERR_TIMEOUT;
    }
    if (ready < 0 || (descriptor.revents & events) == 0) {
        return SPW_ERR_BACKEND;
    }
    return SPW_OK;
}

static bool peer_from_sockaddr(const struct sockaddr_in* address,
                               spw_transport_peer_id_t* out_peer) {
    uint8_t bytes[6];
    if (address == NULL || out_peer == NULL || address->sin_family != AF_INET) {
        return false;
    }
    memcpy(bytes, &address->sin_addr.s_addr, 4u);
    memcpy(bytes + 4u, &address->sin_port, 2u);
    return spw_transport_peer_id_set(out_peer, bytes, sizeof(bytes));
}

static void close_socket(spw_udp_transport_t* transport) {
    if (transport->socket_fd >= 0) {
        (void)close(transport->socket_fd);
        transport->socket_fd = -1;
    }
}

static spw_result_t udp_provider_start(void* context) {
    spw_udp_transport_t* transport = (spw_udp_transport_t*)context;
    if (transport == NULL || transport->socket_fd < 0) {
        return SPW_ERR_INVALID_STATE;
    }
    transport->started = true;
    return SPW_OK;
}

static spw_result_t udp_provider_stop(void* context) {
    spw_udp_transport_t* transport = (spw_udp_transport_t*)context;
    if (transport == NULL || transport->socket_fd < 0) {
        return SPW_ERR_INVALID_STATE;
    }
    transport->started = false;
    return SPW_OK;
}

static spw_result_t udp_provider_reset(void* context) {
    return udp_provider_stop(context);
}

static spw_result_t udp_provider_send(void* context,
                                      const spw_transport_peer_id_t* peer,
                                      const uint8_t* message,
                                      size_t message_size,
                                      spw_timeout_us_t timeout_us) {
    spw_udp_transport_t* transport = (spw_udp_transport_t*)context;
    const struct sockaddr_in* remote;
    if (transport == NULL || peer == NULL ||
        (message_size != 0u && message == NULL)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (transport->socket_fd < 0 || !transport->started) {
        return SPW_ERR_INVALID_STATE;
    }
    if (!spw_transport_peer_id_equal(peer, &transport->remote_peer)) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    if (message_size > SPW_VSPW_TP_MAX_UDP_PAYLOAD) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }
    remote = transport->remote_address_storage;

#if defined(MSG_DONTWAIT)
    {
        spw_udp_deadline_t deadline = deadline_make(timeout_us);
        for (;;) {
            const ssize_t sent = sendto(
                transport->socket_fd, message, message_size, MSG_DONTWAIT,
                (const struct sockaddr*)remote, sizeof(*remote));
            if (sent == (ssize_t)message_size) {
                return SPW_OK;
            }
            if (sent >= 0) {
                return SPW_ERR_BACKEND;
            }
            if (errno == EINTR) {
                if (!deadline.infinite && deadline_expired(&deadline)) {
                    return SPW_ERR_TIMEOUT;
                }
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                return SPW_ERR_BACKEND;
            }
            {
                const spw_result_t wait_result =
                    wait_socket(transport->socket_fd, POLLOUT,
                                deadline_remaining(&deadline));
                if (wait_result != SPW_OK) {
                    return wait_result;
                }
            }
        }
    }
#else
    {
        spw_result_t wait_result =
            wait_socket(transport->socket_fd, POLLOUT, timeout_us);
        ssize_t sent;
        if (wait_result != SPW_OK) {
            return wait_result;
        }
        sent = sendto(transport->socket_fd, message, message_size, 0,
                      (const struct sockaddr*)remote, sizeof(*remote));
        return sent == (ssize_t)message_size ? SPW_OK : SPW_ERR_BACKEND;
    }
#endif
}

static spw_result_t udp_provider_receive(
    void* context,
    uint8_t* message,
    size_t message_capacity,
    size_t* out_message_size,
    spw_transport_peer_id_t* out_peer,
    spw_timeout_us_t timeout_us) {
    spw_udp_transport_t* transport = (spw_udp_transport_t*)context;
    struct sockaddr_in source;
    socklen_t source_size = sizeof(source);
    ssize_t received;
    spw_result_t wait_result;

    if (transport == NULL || out_message_size == NULL || out_peer == NULL ||
        (message_capacity != 0u && message == NULL)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_message_size = 0u;
    *out_peer = (spw_transport_peer_id_t)SPW_TRANSPORT_PEER_ID_INITIALIZER;
    if (transport->socket_fd < 0 || !transport->started) {
        return SPW_ERR_INVALID_STATE;
    }

#if defined(MSG_DONTWAIT)
    {
        spw_udp_deadline_t deadline = deadline_make(timeout_us);
        for (;;) {
            memset(&source, 0, sizeof(source));
            source_size = sizeof(source);
            received = recvfrom(transport->socket_fd, message, message_capacity,
                                MSG_DONTWAIT, (struct sockaddr*)&source,
                                &source_size);
            if (received >= 0) {
                break;
            }
            if (errno == EINTR) {
                if (!deadline.infinite && deadline_expired(&deadline)) {
                    return SPW_ERR_TIMEOUT;
                }
                continue;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                return SPW_ERR_BACKEND;
            }
            wait_result = wait_socket(transport->socket_fd, POLLIN,
                                      deadline_remaining(&deadline));
            if (wait_result != SPW_OK) {
                return wait_result;
            }
        }
    }
#else
    wait_result = wait_socket(transport->socket_fd, POLLIN, timeout_us);
    if (wait_result != SPW_OK) {
        return wait_result;
    }
    memset(&source, 0, sizeof(source));
    received = recvfrom(transport->socket_fd, message, message_capacity, 0,
                        (struct sockaddr*)&source, &source_size);
    if (received < 0) {
        return errno == EINTR ? SPW_ERR_TIMEOUT : SPW_ERR_BACKEND;
    }
#endif

    if (!peer_from_sockaddr(&source, out_peer)) {
        return SPW_ERR_BACKEND;
    }
    *out_message_size = (size_t)received;
    return SPW_OK;
}

static spw_result_t udp_provider_wait(void* context,
                                      spw_transport_ready_t interests,
                                      spw_timeout_us_t timeout_us,
                                      spw_transport_ready_t* out_ready) {
    spw_udp_transport_t* transport = (spw_udp_transport_t*)context;
    struct pollfd descriptor;
    int ready;
    if (transport == NULL || out_ready == NULL ||
        interests == SPW_TRANSPORT_READY_NONE ||
        (interests & (uint8_t)~SPW_TRANSPORT_READY_ALL) != 0u) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_ready = SPW_TRANSPORT_READY_NONE;
    if (transport->socket_fd < 0 || !transport->started) {
        return SPW_ERR_INVALID_STATE;
    }

    memset(&descriptor, 0, sizeof(descriptor));
    descriptor.fd = transport->socket_fd;
    if ((interests & SPW_TRANSPORT_READY_RX) != 0u) {
        descriptor.events |= POLLIN;
    }
    if ((interests & SPW_TRANSPORT_READY_TX) != 0u) {
        descriptor.events |= POLLOUT;
    }
    do {
        ready = poll(&descriptor, 1, timeout_ms(timeout_us));
    } while (ready < 0 && errno == EINTR);
    if (ready == 0) {
        return SPW_ERR_TIMEOUT;
    }
    if (ready < 0) {
        return SPW_ERR_BACKEND;
    }
    if ((descriptor.revents & POLLIN) != 0) {
        *out_ready |= SPW_TRANSPORT_READY_RX;
    }
    if ((descriptor.revents & POLLOUT) != 0) {
        *out_ready |= SPW_TRANSPORT_READY_TX;
    }
    return *out_ready == SPW_TRANSPORT_READY_NONE ? SPW_ERR_BACKEND : SPW_OK;
}

static spw_result_t udp_provider_get_mtu(const void* context, size_t* out_mtu) {
    const spw_udp_transport_t* transport =
        (const spw_udp_transport_t*)context;
    if (transport == NULL || out_mtu == NULL || transport->socket_fd < 0) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_mtu = SPW_VSPW_TP_MAX_UDP_PAYLOAD;
    return SPW_OK;
}

static spw_result_t udp_provider_get_state(
    const void* context,
    spw_transport_state_t* out_state) {
    const spw_udp_transport_t* transport =
        (const spw_udp_transport_t*)context;
    if (transport == NULL || out_state == NULL || transport->socket_fd < 0) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_state = transport->started ? SPW_TRANSPORT_STATE_UP
                                    : SPW_TRANSPORT_STATE_DOWN;
    return SPW_OK;
}

static const spw_transport_provider_ops_t UDP_PROVIDER_OPS = {
    udp_provider_start,
    udp_provider_stop,
    udp_provider_reset,
    udp_provider_send,
    udp_provider_receive,
    udp_provider_wait,
    udp_provider_get_mtu,
    udp_provider_get_state
};

spw_result_t spw_udp_transport_init(spw_udp_transport_t* transport,
                                    const spw_udp_config_t* config) {
    struct sockaddr_in* local;
    struct sockaddr_in* remote;
    int reuse = 1;

    if (transport == NULL || config == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    memset(transport, 0, sizeof(*transport));
    transport->socket_fd = -1;

    local = (struct sockaddr_in*)calloc(1u, sizeof(*local));
    remote = (struct sockaddr_in*)calloc(1u, sizeof(*remote));
    if (local == NULL || remote == NULL) {
        free(local);
        free(remote);
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }
    transport->local_address_storage = local;
    transport->remote_address_storage = remote;

    local->sin_family = AF_INET;
    local->sin_port = htons(config->local_port);
    remote->sin_family = AF_INET;
    remote->sin_port = htons(config->remote_port);
    if (inet_pton(AF_INET, config->local_address, &local->sin_addr) != 1 ||
        inet_pton(AF_INET, config->remote_address, &remote->sin_addr) != 1 ||
        !peer_from_sockaddr(remote, &transport->remote_peer)) {
        spw_udp_transport_destroy(transport);
        return SPW_ERR_INVALID_ARGUMENT;
    }

    transport->socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (transport->socket_fd < 0) {
        spw_udp_transport_destroy(transport);
        return SPW_ERR_BACKEND;
    }
    (void)setsockopt(transport->socket_fd, SOL_SOCKET, SO_REUSEADDR,
                     &reuse, sizeof(reuse));
    if (bind(transport->socket_fd, (const struct sockaddr*)local,
             sizeof(*local)) != 0) {
        spw_udp_transport_destroy(transport);
        return SPW_ERR_BACKEND;
    }
    return SPW_OK;
}

void spw_udp_transport_destroy(spw_udp_transport_t* transport) {
    if (transport == NULL) {
        return;
    }
    close_socket(transport);
    free(transport->local_address_storage);
    free(transport->remote_address_storage);
    transport->local_address_storage = NULL;
    transport->remote_address_storage = NULL;
    transport->started = false;
    transport->remote_peer =
        (spw_transport_peer_id_t)SPW_TRANSPORT_PEER_ID_INITIALIZER;
}

void spw_udp_transport_provider(spw_udp_transport_t* transport,
                                spw_transport_provider_t* out_provider) {
    if (out_provider == NULL) {
        return;
    }
    out_provider->ops = transport == NULL ? NULL : &UDP_PROVIDER_OPS;
    out_provider->context = transport;
}

spw_result_t spw_udp_transport_remote_peer(
    const spw_udp_transport_t* transport,
    spw_transport_peer_id_t* out_peer) {
    if (transport == NULL || out_peer == NULL || transport->socket_fd < 0) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_peer = transport->remote_peer;
    return SPW_OK;
}
