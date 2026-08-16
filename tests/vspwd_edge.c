// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE

#include "backends/device/vspw_device_protocol.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

enum { EDGE_TIMEOUT_MS = 5000 };

typedef struct edge_client {
    int fd;
    uint32_t port_id;
    uint32_t next_request_id;
    uint32_t next_message_id;
} edge_client_t;

static int64_t now_ms(void) {
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static bool poll_for(int fd, short events, int timeout_ms) {
    struct pollfd pfd = {fd, events, 0};
    int result;
    do {
        result = poll(&pfd, 1u, timeout_ms);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (pfd.revents & events) != 0;
}

static bool send_record(int fd, const uint8_t* frame, size_t size) {
    int64_t deadline = now_ms() + EDGE_TIMEOUT_MS;
    for (;;) {
        ssize_t sent = send(fd, frame, size, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent == (ssize_t)size) {
            return true;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            int64_t remaining = deadline - now_ms();
            if (remaining <= 0 || !poll_for(fd, POLLOUT, (int)remaining)) {
                return false;
            }
            continue;
        }
        return false;
    }
}

static ssize_t recv_record(int fd, uint8_t* frame, size_t capacity) {
    struct iovec iov;
    struct msghdr message;
    int64_t deadline = now_ms() + EDGE_TIMEOUT_MS;

    for (;;) {
        ssize_t received;
        int64_t remaining;
        memset(&message, 0, sizeof(message));
        iov.iov_base = frame;
        iov.iov_len = capacity;
        message.msg_iov = &iov;
        message.msg_iovlen = 1u;
        received = recvmsg(fd, &message, MSG_DONTWAIT | MSG_TRUNC);
        if (received >= 0) {
            if ((message.msg_flags & MSG_TRUNC) != 0 ||
                (size_t)received > capacity) {
                return -1;
            }
            return received;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return -1;
        }
        remaining = deadline - now_ms();
        if (remaining <= 0 || !poll_for(fd, POLLIN, (int)remaining)) {
            return -1;
        }
    }
}

static bool connect_client(edge_client_t* client,
                           const char* socket_path,
                           uint32_t port_id) {
    struct sockaddr_un address;
    size_t path_length = strlen(socket_path);
    int64_t deadline = now_ms() + EDGE_TIMEOUT_MS;

    memset(client, 0, sizeof(*client));
    client->fd = -1;
    client->port_id = port_id;
    client->next_request_id = 1u;
    client->next_message_id = 1u;
    if (path_length >= sizeof(address.sun_path)) {
        return false;
    }

    for (;;) {
        client->fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (client->fd < 0) {
            return false;
        }
        memset(&address, 0, sizeof(address));
        address.sun_family = AF_UNIX;
        memcpy(address.sun_path, socket_path, path_length + 1u);
        if (connect(client->fd,
                    (const struct sockaddr*)&address,
                    sizeof(address)) == 0) {
            return true;
        }
        close(client->fd);
        client->fd = -1;
        if ((errno != ENOENT && errno != ECONNREFUSED) || now_ms() >= deadline) {
            return false;
        }
        {
            const struct timespec delay = {0, 20 * 1000 * 1000};
            nanosleep(&delay, NULL);
        }
    }
}

static bool wait_response(edge_client_t* client,
                          uint8_t expected_type,
                          uint32_t expected_request_id,
                          int32_t* status,
                          uint8_t* payload,
                          uint32_t* payload_size) {
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];
    int64_t deadline = now_ms() + EDGE_TIMEOUT_MS;

    for (;;) {
        vspd_header_t header;
        ssize_t received;
        const uint8_t* record_payload;
        if (now_ms() >= deadline) {
            return false;
        }
        received = recv_record(client->fd, frame, sizeof(frame));
        if (received <= 0 ||
            vspd_validate_frame(frame, (size_t)received, &header) != VSPD_CODEC_OK) {
            return false;
        }
        if ((header.flags & VSPD_FLAG_RESPONSE) == 0u) {
            /* State/data/time-code events may legally interleave responses. */
            continue;
        }
        if (header.type != expected_type ||
            header.request_id != expected_request_id) {
            return false;
        }
        record_payload = header.payload_size == 0u
                             ? NULL
                             : frame + VSPD_HEADER_SIZE;
        if (status != NULL) {
            *status = header.status;
        }
        if (payload_size != NULL) {
            if (payload != NULL && header.payload_size != 0u) {
                memcpy(payload, record_payload, header.payload_size);
            }
            *payload_size = header.payload_size;
        }
        return true;
    }
}

static bool request(edge_client_t* client,
                    uint8_t type,
                    const uint8_t* payload,
                    uint32_t payload_size,
                    int32_t* status,
                    uint8_t* response_payload,
                    uint32_t* response_size) {
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_STATISTICS_PAYLOAD_SIZE];
    vspd_header_t header;
    uint32_t request_id = client->next_request_id++;

    memset(&header, 0, sizeof(header));
    header.magic = VSPD_MAGIC;
    header.version_major = VSPD_VERSION_MAJOR;
    header.version_minor = VSPD_VERSION_MINOR;
    header.type = type;
    header.header_size = VSPD_HEADER_SIZE;
    header.payload_size = payload_size;
    header.request_id = request_id;
    header.port_id = client->port_id;
    if (vspd_encode_header(&header, frame) != VSPD_CODEC_OK) {
        return false;
    }
    if (payload_size != 0u) {
        memcpy(frame + VSPD_HEADER_SIZE, payload, payload_size);
    }
    if (!send_record(client->fd, frame, VSPD_HEADER_SIZE + payload_size)) {
        return false;
    }
    return wait_response(client,
                         type,
                         request_id,
                         status,
                         response_payload,
                         response_size);
}

static bool hello(edge_client_t* client) {
    const uint8_t hello_payload[VSPD_HELLO_PAYLOAD_SIZE] = {
        VSPD_VERSION_MAJOR, VSPD_VERSION_MINOR, 0u, 0u};
    uint8_t response[VSPD_HELLO_PAYLOAD_SIZE];
    uint32_t response_size = 0u;
    int32_t status = VSPD_STATUS_BACKEND;
    return request(client,
                   VSPD_MSG_HELLO,
                   hello_payload,
                   sizeof(hello_payload),
                   &status,
                   response,
                   &response_size) &&
           status == VSPD_STATUS_OK && response_size == sizeof(hello_payload) &&
           memcmp(response, hello_payload, sizeof(hello_payload)) == 0;
}

static bool attach(edge_client_t* client, int32_t* status) {
    return request(client,
                   VSPD_MSG_ATTACH,
                   NULL,
                   0u,
                   status,
                   NULL,
                   NULL);
}

static bool start(edge_client_t* client) {
    int32_t status = VSPD_STATUS_BACKEND;
    return request(client,
                   VSPD_MSG_START,
                   NULL,
                   0u,
                   &status,
                   NULL,
                   NULL) &&
           status == VSPD_STATUS_OK;
}

static bool query_state(edge_client_t* client, uint32_t* state) {
    uint8_t payload[VSPD_LINK_STATE_PAYLOAD_SIZE];
    uint32_t payload_size = 0u;
    int32_t status = VSPD_STATUS_BACKEND;
    if (!request(client,
                 VSPD_MSG_GET_LINK_STATE,
                 NULL,
                 0u,
                 &status,
                 payload,
                 &payload_size) ||
        status != VSPD_STATUS_OK || payload_size != sizeof(payload)) {
        return false;
    }
    *state = vspd_decode_u32_payload(payload);
    return true;
}

static bool wait_run(edge_client_t* client) {
    int64_t deadline = now_ms() + EDGE_TIMEOUT_MS;
    while (now_ms() < deadline) {
        uint32_t state = VSPD_LINK_ERROR_RESET;
        if (query_state(client, &state) && state == VSPD_LINK_RUN) {
            return true;
        }
    }
    return false;
}

static bool send_small_packet(edge_client_t* client,
                              const uint8_t* data,
                              uint32_t size,
                              uint8_t terminator) {
    uint8_t frame[VSPD_HEADER_SIZE + 32u];
    vspd_header_t header;
    uint32_t request_id = client->next_request_id++;
    int32_t status = VSPD_STATUS_BACKEND;
    assert(size <= 32u);

    memset(&header, 0, sizeof(header));
    header.magic = VSPD_MAGIC;
    header.version_major = VSPD_VERSION_MAJOR;
    header.version_minor = VSPD_VERSION_MINOR;
    header.type = VSPD_MSG_DATA_TX;
    header.flags = VSPD_FLAG_FRAGMENT_START |
                   VSPD_FLAG_FRAGMENT_END |
                   terminator;
    header.header_size = VSPD_HEADER_SIZE;
    header.payload_size = size;
    header.request_id = request_id;
    header.port_id = client->port_id;
    header.message_id = client->next_message_id++;
    header.total_size = size;
    if (vspd_encode_header(&header, frame) != VSPD_CODEC_OK) {
        return false;
    }
    if (size != 0u) {
        memcpy(frame + VSPD_HEADER_SIZE, data, size);
    }
    return send_record(client->fd, frame, VSPD_HEADER_SIZE + size) &&
           wait_response(client,
                         VSPD_MSG_DATA_TX,
                         request_id,
                         &status,
                         NULL,
                         NULL) &&
           status == VSPD_STATUS_OK;
}

static bool receive_small_packet(edge_client_t* client,
                                 const uint8_t* expected,
                                 uint32_t expected_size,
                                 uint8_t terminator) {
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];
    int64_t deadline = now_ms() + EDGE_TIMEOUT_MS;

    while (now_ms() < deadline) {
        vspd_header_t header;
        ssize_t received = recv_record(client->fd, frame, sizeof(frame));
        if (received <= 0 ||
            vspd_validate_frame(frame, (size_t)received, &header) != VSPD_CODEC_OK) {
            return false;
        }
        if (header.type != VSPD_MSG_DATA_RX) {
            continue;
        }
        if (header.total_size != expected_size ||
            header.payload_size != expected_size ||
            (header.flags & (VSPD_FLAG_FRAGMENT_START | VSPD_FLAG_FRAGMENT_END)) !=
                (VSPD_FLAG_FRAGMENT_START | VSPD_FLAG_FRAGMENT_END) ||
            (header.flags & (VSPD_FLAG_EOP | VSPD_FLAG_EEP)) != terminator) {
            return false;
        }
        return expected_size == 0u ||
               memcmp(frame + VSPD_HEADER_SIZE, expected, expected_size) == 0;
    }
    return false;
}

static bool send_time_code(edge_client_t* client,
                           uint8_t count,
                           uint8_t control) {
    uint8_t payload[VSPD_TIME_CODE_PAYLOAD_SIZE] = {count, control};
    int32_t status = VSPD_STATUS_BACKEND;
    return request(client,
                   VSPD_MSG_TIME_CODE_TX,
                   payload,
                   sizeof(payload),
                   &status,
                   NULL,
                   NULL) &&
           status == VSPD_STATUS_OK;
}

static bool receive_time_code(edge_client_t* client,
                              uint8_t count,
                              uint8_t control) {
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];
    int64_t deadline = now_ms() + EDGE_TIMEOUT_MS;

    while (now_ms() < deadline) {
        vspd_header_t header;
        ssize_t received = recv_record(client->fd, frame, sizeof(frame));
        if (received <= 0 ||
            vspd_validate_frame(frame, (size_t)received, &header) != VSPD_CODEC_OK) {
            return false;
        }
        if (header.type != VSPD_MSG_TIME_CODE_RX) {
            continue;
        }
        return frame[VSPD_HEADER_SIZE] == count &&
               frame[VSPD_HEADER_SIZE + 1u] == control;
    }
    return false;
}

static bool get_statistics(edge_client_t* client,
                           vspd_statistics_payload_t* statistics) {
    uint8_t payload[VSPD_STATISTICS_PAYLOAD_SIZE];
    uint32_t payload_size = 0u;
    int32_t status = VSPD_STATUS_BACKEND;
    if (!request(client,
                 VSPD_MSG_GET_STATISTICS,
                 NULL,
                 0u,
                 &status,
                 payload,
                 &payload_size) ||
        status != VSPD_STATUS_OK || payload_size != sizeof(payload)) {
        return false;
    }
    vspd_decode_statistics(payload, statistics);
    return true;
}

static bool clear_statistics(edge_client_t* client) {
    int32_t status = VSPD_STATUS_BACKEND;
    return request(client,
                   VSPD_MSG_CLEAR_STATISTICS,
                   NULL,
                   0u,
                   &status,
                   NULL,
                   NULL) &&
           status == VSPD_STATUS_OK;
}

static bool malformed_client_is_closed(const char* socket_path) {
    edge_client_t client;
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_HELLO_PAYLOAD_SIZE];
    const uint8_t hello_payload[VSPD_HELLO_PAYLOAD_SIZE] = {
        VSPD_VERSION_MAJOR, VSPD_VERSION_MINOR, 0u, 0u};
    vspd_header_t header;
    struct pollfd pfd;
    ssize_t received;

    if (!connect_client(&client, socket_path, 0u)) {
        return false;
    }
    memset(&header, 0, sizeof(header));
    header.magic = VSPD_MAGIC;
    header.version_major = VSPD_VERSION_MAJOR;
    header.version_minor = VSPD_VERSION_MINOR;
    header.type = VSPD_MSG_HELLO;
    header.header_size = VSPD_HEADER_SIZE;
    header.payload_size = VSPD_HELLO_PAYLOAD_SIZE;
    header.request_id = 1u;
    if (vspd_encode_header(&header, frame) != VSPD_CODEC_OK) {
        close(client.fd);
        return false;
    }
    memcpy(frame + VSPD_HEADER_SIZE, hello_payload, sizeof(hello_payload));
    frame[10] = 0x00u;
    frame[11] = 0x01u; /* non-zero reserved field */
    if (!send_record(client.fd, frame, sizeof(frame))) {
        close(client.fd);
        return false;
    }

    pfd.fd = client.fd;
    pfd.events = POLLIN | POLLHUP | POLLERR;
    pfd.revents = 0;
    if (poll(&pfd, 1u, EDGE_TIMEOUT_MS) <= 0) {
        close(client.fd);
        return false;
    }
    received = recv(client.fd, frame, sizeof(frame), 0);
    close(client.fd);
    return received == 0;
}

int main(int argc, char** argv) {
    edge_client_t port0;
    edge_client_t port1;
    edge_client_t duplicate;
    const uint8_t packet[] = {0x45u, 0x44u, 0x47u, 0x45u};
    vspd_statistics_payload_t statistics;
    int32_t status = VSPD_STATUS_BACKEND;

    if (argc != 2) {
        fprintf(stderr, "usage: %s SOCKET\n", argv[0]);
        return 2;
    }

    assert(connect_client(&port0, argv[1], 0u));
    assert(hello(&port0));
    assert(attach(&port0, &status));
    assert(status == VSPD_STATUS_OK);
    assert(start(&port0));

    assert(connect_client(&port1, argv[1], 1u));
    assert(hello(&port1));
    assert(attach(&port1, &status));
    assert(status == VSPD_STATUS_OK);
    assert(start(&port1));
    assert(wait_run(&port0));
    assert(wait_run(&port1));

    assert(send_small_packet(&port0, packet, sizeof(packet), VSPD_FLAG_EEP));
    assert(receive_small_packet(&port1, packet, sizeof(packet), VSPD_FLAG_EEP));
    assert(send_time_code(&port1, 21u, 0u));
    assert(receive_time_code(&port0, 21u, 0u));

    assert(get_statistics(&port0, &statistics));
    assert(statistics.tx_packets == 1u);
    assert(statistics.tx_bytes == sizeof(packet));
    assert(statistics.rx_time_codes == 1u);
    assert(statistics.eep_packets == 1u);
    assert(clear_statistics(&port0));
    assert(get_statistics(&port0, &statistics));
    assert(statistics.tx_packets == 0u);
    assert(statistics.tx_bytes == 0u);
    assert(statistics.rx_time_codes == 0u);
    assert(statistics.eep_packets == 0u);

    assert(connect_client(&duplicate, argv[1], 0u));
    assert(hello(&duplicate));
    assert(attach(&duplicate, &status));
    assert(status == VSPD_STATUS_RESOURCE_EXHAUSTED);
    close(duplicate.fd);

    assert(malformed_client_is_closed(argv[1]));

    close(port1.fd);
    close(port0.fd);
    return 0;
}
