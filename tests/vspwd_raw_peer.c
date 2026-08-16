// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L

#include "backends/device/vspw_device_protocol.h"

#include <assert.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

enum {
    TEST_TIMEOUT_MS = 5000,
    TEST_BIG_PACKET_SIZE = 70000
};

typedef struct test_packet {
    bool ready;
    uint32_t message_id;
    uint32_t total_size;
    uint32_t next_offset;
    uint8_t terminator_flags;
    uint8_t data[VSPD_MAX_LOGICAL_PACKET];
} test_packet_t;

typedef struct test_client {
    int fd;
    uint32_t port_id;
    uint32_t next_request_id;
    uint32_t next_message_id;
    uint32_t state;
    bool state_valid;
    bool time_code_ready;
    uint8_t time_count;
    uint8_t control_flags;
    test_packet_t packet;
} test_client_t;

static int64_t monotonic_ms(void) {
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC, &now) == 0);
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static bool wait_fd(int fd, short events, int timeout_ms) {
    struct pollfd descriptor;
    int result;
    descriptor.fd = fd;
    descriptor.events = events;
    descriptor.revents = 0;
    do {
        result = poll(&descriptor, 1u, timeout_ms);
    } while (result < 0 && errno == EINTR);
    return result > 0 && (descriptor.revents & events) != 0;
}

static bool send_record(int fd, const uint8_t* data, size_t size) {
    int64_t deadline = monotonic_ms() + TEST_TIMEOUT_MS;
    for (;;) {
        ssize_t sent = send(fd, data, size, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (sent == (ssize_t)size) {
            return true;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            int64_t remaining = deadline - monotonic_ms();
            if (remaining <= 0 || !wait_fd(fd, POLLOUT, (int)remaining)) {
                return false;
            }
            continue;
        }
        return false;
    }
}

static ssize_t recv_record(int fd, uint8_t* frame, size_t capacity, int timeout_ms) {
    struct iovec iov;
    struct msghdr message;
    int64_t deadline = monotonic_ms() + timeout_ms;

    memset(&message, 0, sizeof(message));
    iov.iov_base = frame;
    iov.iov_len = capacity;
    message.msg_iov = &iov;
    message.msg_iovlen = 1u;

    for (;;) {
        ssize_t received = recvmsg(fd, &message, MSG_DONTWAIT | MSG_TRUNC);
        if (received >= 0) {
            if ((message.msg_flags & MSG_TRUNC) != 0 ||
                (size_t)received > capacity) {
                return -1;
            }
            return received;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            int64_t remaining = deadline - monotonic_ms();
            if (remaining <= 0 || !wait_fd(fd, POLLIN, (int)remaining)) {
                return -1;
            }
            memset(&message, 0, sizeof(message));
            iov.iov_base = frame;
            iov.iov_len = capacity;
            message.msg_iov = &iov;
            message.msg_iovlen = 1u;
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        return -1;
    }
}

static bool connect_client(test_client_t* client,
                           const char* socket_path,
                           uint32_t port_id) {
    struct sockaddr_un address;
    size_t length = strlen(socket_path);
    int64_t deadline = monotonic_ms() + TEST_TIMEOUT_MS;

    memset(client, 0, sizeof(*client));
    client->fd = -1;
    client->port_id = port_id;
    client->next_request_id = 1u;
    client->next_message_id = 1u;

    if (length >= sizeof(address.sun_path)) {
        return false;
    }

    for (;;) {
        client->fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
        if (client->fd < 0) {
            return false;
        }
        memset(&address, 0, sizeof(address));
        address.sun_family = AF_UNIX;
        memcpy(address.sun_path, socket_path, length + 1u);
        if (connect(client->fd,
                    (const struct sockaddr*)&address,
                    sizeof(address)) == 0) {
            return true;
        }
        close(client->fd);
        client->fd = -1;
        if ((errno != ENOENT && errno != ECONNREFUSED) ||
            monotonic_ms() >= deadline) {
            return false;
        }
        {
            struct timespec delay = {0, 20 * 1000 * 1000};
            nanosleep(&delay, NULL);
        }
    }
}

static bool process_event(test_client_t* client,
                          const vspd_header_t* header,
                          const uint8_t* payload) {
    if (header->type == VSPD_MSG_LINK_STATE_EVENT) {
        client->state = vspd_decode_u32_payload(payload);
        client->state_valid = true;
        return true;
    }
    if (header->type == VSPD_MSG_TIME_CODE_RX) {
        client->time_count = payload[0];
        client->control_flags = payload[1];
        client->time_code_ready = true;
        return true;
    }
    if (header->type == VSPD_MSG_DATA_RX) {
        test_packet_t* packet = &client->packet;
        bool start = (header->flags & VSPD_FLAG_FRAGMENT_START) != 0u;
        bool end = (header->flags & VSPD_FLAG_FRAGMENT_END) != 0u;
        if (start) {
            if (packet->next_offset != 0u || packet->ready) {
                return false;
            }
            packet->message_id = header->message_id;
            packet->total_size = header->total_size;
        } else if (packet->message_id != header->message_id ||
                   packet->total_size != header->total_size) {
            return false;
        }
        if (header->fragment_offset != packet->next_offset ||
            header->fragment_offset + header->payload_size > sizeof(packet->data)) {
            return false;
        }
        if (header->payload_size != 0u) {
            memcpy(packet->data + header->fragment_offset,
                   payload,
                   header->payload_size);
        }
        packet->next_offset += header->payload_size;
        if (end) {
            if (packet->next_offset != packet->total_size) {
                return false;
            }
            packet->terminator_flags =
                header->flags & (VSPD_FLAG_EOP | VSPD_FLAG_EEP);
            packet->ready = true;
        }
        return true;
    }
    return false;
}

static bool wait_response(test_client_t* client,
                          uint8_t type,
                          uint32_t request_id,
                          int32_t* out_status,
                          uint8_t* payload,
                          uint32_t* payload_size) {
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];
    int64_t deadline = monotonic_ms() + TEST_TIMEOUT_MS;

    for (;;) {
        vspd_header_t header;
        ssize_t received;
        int64_t remaining = deadline - monotonic_ms();
        const uint8_t* record_payload;
        if (remaining <= 0) {
            return false;
        }
        received = recv_record(client->fd, frame, sizeof(frame), (int)remaining);
        if (received <= 0 ||
            vspd_validate_frame(frame, (size_t)received, &header) != VSPD_CODEC_OK) {
            return false;
        }
        record_payload = header.payload_size == 0u
                             ? NULL
                             : frame + VSPD_HEADER_SIZE;
        if ((header.flags & VSPD_FLAG_RESPONSE) != 0u) {
            if (header.type != type || header.request_id != request_id) {
                return false;
            }
            if (out_status != NULL) {
                *out_status = header.status;
            }
            if (payload_size != NULL) {
                if (payload != NULL && header.payload_size != 0u) {
                    memcpy(payload, record_payload, header.payload_size);
                }
                *payload_size = header.payload_size;
            }
            return true;
        }
        if (!process_event(client, &header, record_payload)) {
            return false;
        }
    }
}

static bool request(test_client_t* client,
                    uint8_t type,
                    const uint8_t* payload,
                    uint32_t payload_size,
                    int32_t* out_status,
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
                         out_status,
                         response_payload,
                         response_size);
}

static bool hello_attach_start(test_client_t* client,
                               const char* socket_path,
                               uint32_t port_id) {
    uint8_t hello[4] = {VSPD_VERSION_MAJOR, VSPD_VERSION_MINOR, 0u, 0u};
    uint8_t response[72];
    uint32_t response_size = 0u;
    int32_t status = 0;

    if (!connect_client(client, socket_path, port_id)) {
        return false;
    }
    if (!request(client,
                 VSPD_MSG_HELLO,
                 hello,
                 sizeof(hello),
                 &status,
                 response,
                 &response_size) ||
        status != VSPD_STATUS_OK || response_size != sizeof(hello) ||
        memcmp(response, hello, sizeof(hello)) != 0) {
        return false;
    }
    if (!request(client,
                 VSPD_MSG_ATTACH,
                 NULL,
                 0u,
                 &status,
                 NULL,
                 NULL) ||
        status != VSPD_STATUS_OK) {
        return false;
    }
    if (!request(client,
                 VSPD_MSG_START,
                 NULL,
                 0u,
                 &status,
                 NULL,
                 NULL) ||
        status != VSPD_STATUS_OK) {
        return false;
    }
    return true;
}

static bool query_state(test_client_t* client, uint32_t* state) {
    uint8_t response[4];
    uint32_t response_size = 0u;
    int32_t status = 0;
    if (!request(client,
                 VSPD_MSG_GET_LINK_STATE,
                 NULL,
                 0u,
                 &status,
                 response,
                 &response_size) ||
        status != VSPD_STATUS_OK || response_size != 4u) {
        return false;
    }
    *state = vspd_decode_u32_payload(response);
    client->state = *state;
    client->state_valid = true;
    return true;
}

static bool wait_state(test_client_t* client, uint32_t target) {
    int64_t deadline = monotonic_ms() + TEST_TIMEOUT_MS;
    for (;;) {
        uint32_t state;
        if (query_state(client, &state) && state == target) {
            return true;
        }
        if (monotonic_ms() >= deadline) {
            return false;
        }
        {
            struct timespec delay = {0, 20 * 1000 * 1000};
            nanosleep(&delay, NULL);
        }
    }
}

static bool send_packet(test_client_t* client,
                        const uint8_t* data,
                        uint32_t length,
                        uint8_t terminator_flag) {
    uint32_t request_id = client->next_request_id++;
    uint32_t message_id = client->next_message_id++;
    uint32_t offset = 0u;
    bool sent_zero = false;
    int32_t status = 0;

    do {
        uint8_t frame[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];
        vspd_header_t header;
        uint32_t remaining = length - offset;
        uint32_t chunk = remaining > VSPD_MAX_FRAME_PAYLOAD
                             ? VSPD_MAX_FRAME_PAYLOAD
                             : remaining;
        bool final_fragment = length == 0u || offset + chunk == length;

        memset(&header, 0, sizeof(header));
        header.magic = VSPD_MAGIC;
        header.version_major = VSPD_VERSION_MAJOR;
        header.version_minor = VSPD_VERSION_MINOR;
        header.type = VSPD_MSG_DATA_TX;
        header.header_size = VSPD_HEADER_SIZE;
        header.payload_size = chunk;
        header.request_id = request_id;
        header.port_id = client->port_id;
        header.message_id = message_id;
        header.fragment_offset = offset;
        header.total_size = length;
        if (offset == 0u) {
            header.flags |= VSPD_FLAG_FRAGMENT_START;
        }
        if (final_fragment) {
            header.flags |= VSPD_FLAG_FRAGMENT_END | terminator_flag;
        }
        if (vspd_encode_header(&header, frame) != VSPD_CODEC_OK) {
            return false;
        }
        if (chunk != 0u) {
            memcpy(frame + VSPD_HEADER_SIZE, data + offset, chunk);
        }
        if (!send_record(client->fd, frame, VSPD_HEADER_SIZE + chunk)) {
            return false;
        }
        offset += chunk;
        sent_zero = true;
    } while (offset < length || !sent_zero);

    return wait_response(client,
                         VSPD_MSG_DATA_TX,
                         request_id,
                         &status,
                         NULL,
                         NULL) &&
           status == VSPD_STATUS_OK;
}

static bool receive_packet(test_client_t* client,
                           const uint8_t* expected,
                           uint32_t expected_size,
                           uint8_t terminator_flag) {
    int64_t deadline = monotonic_ms() + TEST_TIMEOUT_MS;
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];

    memset(&client->packet, 0, sizeof(client->packet));
    while (!client->packet.ready) {
        vspd_header_t header;
        ssize_t received;
        int64_t remaining = deadline - monotonic_ms();
        const uint8_t* payload;
        if (remaining <= 0) {
            return false;
        }
        received = recv_record(client->fd, frame, sizeof(frame), (int)remaining);
        if (received <= 0 ||
            vspd_validate_frame(frame, (size_t)received, &header) != VSPD_CODEC_OK ||
            (header.flags & VSPD_FLAG_RESPONSE) != 0u) {
            return false;
        }
        payload = header.payload_size == 0u ? NULL : frame + VSPD_HEADER_SIZE;
        if (!process_event(client, &header, payload)) {
            return false;
        }
    }

    if (client->packet.total_size != expected_size ||
        client->packet.terminator_flags != terminator_flag) {
        return false;
    }
    return expected_size == 0u ||
           memcmp(client->packet.data, expected, expected_size) == 0;
}

static bool send_time_code(test_client_t* client,
                           uint8_t time_count,
                           uint8_t control_flags) {
    uint8_t payload[2] = {time_count, control_flags};
    int32_t status = 0;
    return request(client,
                   VSPD_MSG_TIME_CODE_TX,
                   payload,
                   sizeof(payload),
                   &status,
                   NULL,
                   NULL) &&
           status == VSPD_STATUS_OK;
}

static bool receive_time_code(test_client_t* client,
                              uint8_t time_count,
                              uint8_t control_flags) {
    int64_t deadline = monotonic_ms() + TEST_TIMEOUT_MS;
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];
    client->time_code_ready = false;
    while (!client->time_code_ready) {
        vspd_header_t header;
        ssize_t received;
        int64_t remaining = deadline - monotonic_ms();
        const uint8_t* payload;
        if (remaining <= 0) {
            return false;
        }
        received = recv_record(client->fd, frame, sizeof(frame), (int)remaining);
        if (received <= 0 ||
            vspd_validate_frame(frame, (size_t)received, &header) != VSPD_CODEC_OK ||
            (header.flags & VSPD_FLAG_RESPONSE) != 0u) {
            return false;
        }
        payload = header.payload_size == 0u ? NULL : frame + VSPD_HEADER_SIZE;
        if (!process_event(client, &header, payload)) {
            return false;
        }
    }
    return client->time_count == time_count &&
           client->control_flags == control_flags;
}

static void fill_pattern(uint8_t* data, size_t size, uint8_t seed) {
    size_t i;
    for (i = 0u; i < size; ++i) {
        data[i] = (uint8_t)(seed + (uint8_t)(i * 29u));
    }
}

static int run_survivor(const char* socket_path) {
    test_client_t client;
    static uint8_t big[TEST_BIG_PACKET_SIZE];
    static const uint8_t reply[] = {0x42u, 0x2du, 0x3eu, 0x41u};
    static const uint8_t restart_reply[] = {0x52u, 0x45u, 0x53u, 0x54u};
    fill_pattern(big, sizeof(big), 0x31u);

    if (!hello_attach_start(&client, socket_path, 0u) ||
        !wait_state(&client, VSPD_LINK_RUN) ||
        !send_packet(&client, big, sizeof(big), VSPD_FLAG_EOP) ||
        !receive_packet(&client, reply, sizeof(reply), VSPD_FLAG_EEP) ||
        !receive_time_code(&client, 17u, 0u) ||
        !wait_state(&client, VSPD_LINK_ERROR_WAIT) ||
        !wait_state(&client, VSPD_LINK_RUN) ||
        !send_packet(&client, NULL, 0u, VSPD_FLAG_EEP) ||
        !receive_packet(&client,
                        restart_reply,
                        sizeof(restart_reply),
                        VSPD_FLAG_EOP)) {
        if (client.fd >= 0) {
            close(client.fd);
        }
        return 1;
    }
    close(client.fd);
    return 0;
}

static int run_initial(const char* socket_path) {
    test_client_t client;
    static uint8_t big[TEST_BIG_PACKET_SIZE];
    static const uint8_t reply[] = {0x42u, 0x2du, 0x3eu, 0x41u};
    fill_pattern(big, sizeof(big), 0x31u);

    if (!hello_attach_start(&client, socket_path, 1u) ||
        !wait_state(&client, VSPD_LINK_RUN) ||
        !receive_packet(&client, big, sizeof(big), VSPD_FLAG_EOP) ||
        !send_packet(&client, reply, sizeof(reply), VSPD_FLAG_EEP) ||
        !send_time_code(&client, 17u, 0u)) {
        if (client.fd >= 0) {
            close(client.fd);
        }
        return 1;
    }
    close(client.fd);
    return 0;
}

static int run_restart(const char* socket_path) {
    test_client_t client;
    static const uint8_t reply[] = {0x52u, 0x45u, 0x53u, 0x54u};

    if (!hello_attach_start(&client, socket_path, 1u) ||
        !wait_state(&client, VSPD_LINK_RUN) ||
        !receive_packet(&client, NULL, 0u, VSPD_FLAG_EEP) ||
        !send_packet(&client, reply, sizeof(reply), VSPD_FLAG_EOP)) {
        if (client.fd >= 0) {
            close(client.fd);
        }
        return 1;
    }
    close(client.fd);
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s SOCKET survivor|initial|restart\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[2], "survivor") == 0) {
        return run_survivor(argv[1]);
    }
    if (strcmp(argv[2], "initial") == 0) {
        return run_initial(argv[1]);
    }
    if (strcmp(argv[2], "restart") == 0) {
        return run_restart(argv[1]);
    }
    return 2;
}
