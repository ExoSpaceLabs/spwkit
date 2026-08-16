from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, got {count}")
    p.write_text(text.replace(old, new, 1))


def write(path: str, content: str) -> None:
    p = Path(path)
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content)


# ---------------------------------------------------------------------------
# VSPD 1.2: passive monitor subscriptions and coalesced snapshot events.
# ---------------------------------------------------------------------------
replace_once(
    "src/backends/device/vspw_device_protocol.h",
    "#define VSPD_VERSION_MINOR 1u\n",
    "#define VSPD_VERSION_MINOR 2u\n")

replace_once(
    "src/backends/device/vspw_device_protocol.h",
    "#define VSPD_MSG_GET_PORT_STATISTICS   18u\n#define VSPD_MSG_CLEAR_PORT_STATISTICS 19u\n",
    "#define VSPD_MSG_GET_PORT_STATISTICS   18u\n#define VSPD_MSG_CLEAR_PORT_STATISTICS 19u\n#define VSPD_MSG_SUBSCRIBE_PORT        20u\n#define VSPD_MSG_UNSUBSCRIBE_PORT      21u\n#define VSPD_MSG_PORT_SNAPSHOT_EVENT   22u\n")

replace_once(
    "src/backends/device/vspw_device_protocol.h",
    "#define VSPD_PORT_INFO_PAYLOAD_SIZE    16u\n",
    "#define VSPD_PORT_INFO_PAYLOAD_SIZE    16u\n#define VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE \\\n    (VSPD_PORT_INFO_PAYLOAD_SIZE + VSPD_STATISTICS_PAYLOAD_SIZE)\n")

replace_once(
    "src/backends/device/vspw_device_protocol.h",
    "typedef struct vspd_statistics_payload {\n    uint64_t tx_packets;\n    uint64_t rx_packets;\n    uint64_t tx_bytes;\n    uint64_t rx_bytes;\n    uint64_t tx_time_codes;\n    uint64_t rx_time_codes;\n    uint64_t eep_packets;\n    uint64_t link_errors;\n    uint64_t dropped_packets;\n} vspd_statistics_payload_t;\n",
    "typedef struct vspd_statistics_payload {\n    uint64_t tx_packets;\n    uint64_t rx_packets;\n    uint64_t tx_bytes;\n    uint64_t rx_bytes;\n    uint64_t tx_time_codes;\n    uint64_t rx_time_codes;\n    uint64_t eep_packets;\n    uint64_t link_errors;\n    uint64_t dropped_packets;\n} vspd_statistics_payload_t;\n\ntypedef struct vspd_port_snapshot_payload {\n    vspd_port_info_payload_t info;\n    vspd_statistics_payload_t statistics;\n} vspd_port_snapshot_payload_t;\n")

replace_once(
    "src/backends/device/vspw_device_protocol.h",
    "void vspd_decode_statistics(const uint8_t in[VSPD_STATISTICS_PAYLOAD_SIZE],\n                            vspd_statistics_payload_t* out);\n",
    "void vspd_decode_statistics(const uint8_t in[VSPD_STATISTICS_PAYLOAD_SIZE],\n                            vspd_statistics_payload_t* out);\n\nvoid vspd_encode_port_snapshot(\n    const vspd_port_snapshot_payload_t* value,\n    uint8_t out[VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE]);\nvoid vspd_decode_port_snapshot(\n    const uint8_t in[VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE],\n    vspd_port_snapshot_payload_t* out);\n")

replace_once(
    "src/backends/device/vspw_device_protocol.c",
    "static int vspd_type_valid(uint8_t type) {\n    return type >= VSPD_MSG_HELLO && type <= VSPD_MSG_CLEAR_PORT_STATISTICS;\n}\n",
    "static int vspd_type_valid(uint8_t type) {\n    return type >= VSPD_MSG_HELLO && type <= VSPD_MSG_PORT_SNAPSHOT_EVENT;\n}\n")

replace_once(
    "src/backends/device/vspw_device_protocol.c",
    "    return type == VSPD_MSG_DATA_RX ||\n           type == VSPD_MSG_TIME_CODE_RX ||\n           type == VSPD_MSG_LINK_STATE_EVENT;\n",
    "    return type == VSPD_MSG_DATA_RX ||\n           type == VSPD_MSG_TIME_CODE_RX ||\n           type == VSPD_MSG_LINK_STATE_EVENT ||\n           type == VSPD_MSG_PORT_SNAPSHOT_EVENT;\n")

replace_once(
    "src/backends/device/vspw_device_protocol.c",
    "                case VSPD_MSG_GET_PORT_STATISTICS:\n                case VSPD_MSG_CLEAR_PORT_STATISTICS:\n                    if (header->payload_size != 0u) {\n",
    "                case VSPD_MSG_GET_PORT_STATISTICS:\n                case VSPD_MSG_CLEAR_PORT_STATISTICS:\n                case VSPD_MSG_SUBSCRIBE_PORT:\n                case VSPD_MSG_UNSUBSCRIBE_PORT:\n                    if (header->payload_size != 0u) {\n")

replace_once(
    "src/backends/device/vspw_device_protocol.c",
    "                case VSPD_MSG_LINK_STATE_EVENT:\n                    if (header->payload_size != VSPD_LINK_STATE_PAYLOAD_SIZE) {\n                        return VSPD_CODEC_INVALID_SHAPE;\n                    }\n                    break;\n",
    "                case VSPD_MSG_LINK_STATE_EVENT:\n                    if (header->payload_size != VSPD_LINK_STATE_PAYLOAD_SIZE) {\n                        return VSPD_CODEC_INVALID_SHAPE;\n                    }\n                    break;\n                case VSPD_MSG_PORT_SNAPSHOT_EVENT:\n                    if (header->payload_size != VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE) {\n                        return VSPD_CODEC_INVALID_SHAPE;\n                    }\n                    break;\n")

replace_once(
    "src/backends/device/vspw_device_protocol.c",
    "        if (header->type == VSPD_MSG_GET_PORT_INFO && response &&\n            header->status == VSPD_STATUS_OK &&\n            header->payload_size == VSPD_PORT_INFO_PAYLOAD_SIZE) {\n            const uint32_t flags = vspd_read_be32(payload + 0u);\n            const uint32_t state = vspd_read_be32(payload + 4u);\n            if ((flags & ~VSPD_PORT_INFO_KNOWN_MASK) != 0u ||\n                state > VSPD_LINK_RUN) {\n                return VSPD_CODEC_INVALID_SHAPE;\n            }\n        }\n",
    "        if ((header->type == VSPD_MSG_GET_PORT_INFO && response &&\n             header->status == VSPD_STATUS_OK &&\n             header->payload_size == VSPD_PORT_INFO_PAYLOAD_SIZE) ||\n            (header->type == VSPD_MSG_PORT_SNAPSHOT_EVENT &&\n             header->payload_size == VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE)) {\n            const uint32_t flags = vspd_read_be32(payload + 0u);\n            const uint32_t state = vspd_read_be32(payload + 4u);\n            if ((flags & ~VSPD_PORT_INFO_KNOWN_MASK) != 0u ||\n                state > VSPD_LINK_RUN) {\n                return VSPD_CODEC_INVALID_SHAPE;\n            }\n        }\n")

replace_once(
    "src/backends/device/vspw_device_protocol.c",
    "void vspd_decode_statistics(const uint8_t in[VSPD_STATISTICS_PAYLOAD_SIZE],\n                            vspd_statistics_payload_t* out) {\n    if (in == NULL || out == NULL) {\n        return;\n    }\n    out->tx_packets = vspd_read_be64(in + 0u);\n    out->rx_packets = vspd_read_be64(in + 8u);\n    out->tx_bytes = vspd_read_be64(in + 16u);\n    out->rx_bytes = vspd_read_be64(in + 24u);\n    out->tx_time_codes = vspd_read_be64(in + 32u);\n    out->rx_time_codes = vspd_read_be64(in + 40u);\n    out->eep_packets = vspd_read_be64(in + 48u);\n    out->link_errors = vspd_read_be64(in + 56u);\n    out->dropped_packets = vspd_read_be64(in + 64u);\n}\n",
    "void vspd_decode_statistics(const uint8_t in[VSPD_STATISTICS_PAYLOAD_SIZE],\n                            vspd_statistics_payload_t* out) {\n    if (in == NULL || out == NULL) {\n        return;\n    }\n    out->tx_packets = vspd_read_be64(in + 0u);\n    out->rx_packets = vspd_read_be64(in + 8u);\n    out->tx_bytes = vspd_read_be64(in + 16u);\n    out->rx_bytes = vspd_read_be64(in + 24u);\n    out->tx_time_codes = vspd_read_be64(in + 32u);\n    out->rx_time_codes = vspd_read_be64(in + 40u);\n    out->eep_packets = vspd_read_be64(in + 48u);\n    out->link_errors = vspd_read_be64(in + 56u);\n    out->dropped_packets = vspd_read_be64(in + 64u);\n}\n\nvoid vspd_encode_port_snapshot(\n    const vspd_port_snapshot_payload_t* value,\n    uint8_t out[VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE]) {\n    if (value == NULL || out == NULL) {\n        return;\n    }\n    vspd_encode_port_info(&value->info, out);\n    vspd_encode_statistics(&value->statistics,\n                           out + VSPD_PORT_INFO_PAYLOAD_SIZE);\n}\n\nvoid vspd_decode_port_snapshot(\n    const uint8_t in[VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE],\n    vspd_port_snapshot_payload_t* out) {\n    if (in == NULL || out == NULL) {\n        return;\n    }\n    vspd_decode_port_info(in, &out->info);\n    vspd_decode_statistics(in + VSPD_PORT_INFO_PAYLOAD_SIZE,\n                           &out->statistics);\n}\n")

# ---------------------------------------------------------------------------
# Private management client: subscriptions + bounded event queue.
# ---------------------------------------------------------------------------
write("src/tools/vspd_management_client.h", r'''// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_VSPD_MANAGEMENT_CLIENT_H
#define SPWKIT_VSPD_MANAGEMENT_CLIENT_H

#include "backends/device/vspw_device_protocol.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VSPD_MANAGEMENT_EVENT_QUEUE_DEPTH 8u

typedef struct vspd_management_event {
    uint32_t port_id;
    vspd_port_snapshot_payload_t snapshot;
} vspd_management_event_t;

typedef struct vspd_management_client {
    int fd;
    uint32_t next_request_id;
    uint32_t event_head;
    uint32_t event_tail;
    uint32_t event_count;
    vspd_management_event_t events[VSPD_MANAGEMENT_EVENT_QUEUE_DEPTH];
} vspd_management_client_t;

int32_t vspd_management_open(vspd_management_client_t* client,
                             const char* socket_path);
void vspd_management_close(vspd_management_client_t* client);

int32_t vspd_management_get_server_info(
    vspd_management_client_t* client,
    vspd_server_info_payload_t* out_info);
int32_t vspd_management_get_port_info(
    vspd_management_client_t* client,
    uint32_t port_id,
    vspd_port_info_payload_t* out_info);
int32_t vspd_management_get_port_statistics(
    vspd_management_client_t* client,
    uint32_t port_id,
    vspd_statistics_payload_t* out_statistics);
int32_t vspd_management_clear_port_statistics(
    vspd_management_client_t* client,
    uint32_t port_id);
int32_t vspd_management_subscribe_port(
    vspd_management_client_t* client,
    uint32_t port_id);
int32_t vspd_management_unsubscribe_port(
    vspd_management_client_t* client,
    uint32_t port_id);
int32_t vspd_management_receive_snapshot(
    vspd_management_client_t* client,
    int timeout_ms,
    uint32_t* out_port_id,
    vspd_port_snapshot_payload_t* out_snapshot);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_VSPD_MANAGEMENT_CLIENT_H */
''')

write("src/tools/vspd_management_client.c", r'''// SPDX-License-Identifier: Apache-2.0
#define _GNU_SOURCE

#include "tools/vspd_management_client.h"

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

enum {
    VSPD_MANAGEMENT_TIMEOUT_MS = 2000,
    VSPD_MANAGEMENT_FRAME_MAX = VSPD_HEADER_SIZE + VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE
};

static int wait_fd(int fd, short events, int timeout_ms) {
    struct pollfd descriptor;
    int result;
    descriptor.fd = fd;
    descriptor.events = events;
    descriptor.revents = 0;
    do {
        result = poll(&descriptor, 1u, timeout_ms);
    } while (result < 0 && errno == EINTR);
    if (result <= 0) {
        return result;
    }
    if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        return -1;
    }
    return (descriptor.revents & events) != 0 ? 1 : -1;
}

static bool send_record(int fd, const uint8_t* data, size_t size) {
    ssize_t sent;
    if (wait_fd(fd, POLLOUT, VSPD_MANAGEMENT_TIMEOUT_MS) != 1) {
        return false;
    }
    do {
        sent = send(fd, data, size, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    return sent == (ssize_t)size;
}

static ssize_t receive_record(int fd,
                              uint8_t* frame,
                              size_t capacity,
                              int timeout_ms,
                              bool* out_timed_out) {
    struct iovec iov;
    struct msghdr message;
    ssize_t received;
    int ready;

    if (out_timed_out != NULL) {
        *out_timed_out = false;
    }
    ready = wait_fd(fd, POLLIN, timeout_ms);
    if (ready == 0) {
        if (out_timed_out != NULL) {
            *out_timed_out = true;
        }
        return -1;
    }
    if (ready != 1) {
        return -1;
    }
    memset(&message, 0, sizeof(message));
    iov.iov_base = frame;
    iov.iov_len = capacity;
    message.msg_iov = &iov;
    message.msg_iovlen = 1u;
    do {
        received = recvmsg(fd, &message, MSG_TRUNC);
    } while (received < 0 && errno == EINTR);
    if (received < 0 || (message.msg_flags & MSG_TRUNC) != 0 ||
        (size_t)received > capacity) {
        return -1;
    }
    return received;
}

static uint32_t next_request_id(vspd_management_client_t* client) {
    uint32_t request_id = client->next_request_id++;
    if (request_id == 0u) {
        request_id = client->next_request_id++;
    }
    if (client->next_request_id == 0u) {
        client->next_request_id = 1u;
    }
    return request_id;
}

static bool queue_event(vspd_management_client_t* client,
                        uint32_t port_id,
                        const vspd_port_snapshot_payload_t* snapshot) {
    uint32_t i;
    for (i = 0u; i < client->event_count; ++i) {
        uint32_t index =
            (client->event_head + i) % VSPD_MANAGEMENT_EVENT_QUEUE_DEPTH;
        if (client->events[index].port_id == port_id) {
            client->events[index].snapshot = *snapshot;
            return true;
        }
    }
    if (client->event_count >= VSPD_MANAGEMENT_EVENT_QUEUE_DEPTH) {
        return false;
    }
    client->events[client->event_tail].port_id = port_id;
    client->events[client->event_tail].snapshot = *snapshot;
    client->event_tail =
        (client->event_tail + 1u) % VSPD_MANAGEMENT_EVENT_QUEUE_DEPTH;
    ++client->event_count;
    return true;
}

static bool decode_snapshot_event(vspd_management_client_t* client,
                                  const vspd_header_t* header,
                                  const uint8_t* payload) {
    vspd_port_snapshot_payload_t snapshot;
    if (header->type != VSPD_MSG_PORT_SNAPSHOT_EVENT ||
        header->payload_size != VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE ||
        payload == NULL) {
        return false;
    }
    memset(&snapshot, 0, sizeof(snapshot));
    vspd_decode_port_snapshot(payload, &snapshot);
    return queue_event(client, header->port_id, &snapshot);
}

static bool pop_event(vspd_management_client_t* client,
                      uint32_t* out_port_id,
                      vspd_port_snapshot_payload_t* out_snapshot) {
    if (client->event_count == 0u) {
        return false;
    }
    *out_port_id = client->events[client->event_head].port_id;
    *out_snapshot = client->events[client->event_head].snapshot;
    client->event_head =
        (client->event_head + 1u) % VSPD_MANAGEMENT_EVENT_QUEUE_DEPTH;
    --client->event_count;
    return true;
}

static int32_t request(vspd_management_client_t* client,
                       uint8_t type,
                       uint32_t port_id,
                       const uint8_t* payload,
                       uint32_t payload_size,
                       uint8_t* response_payload,
                       uint32_t response_capacity,
                       uint32_t* out_response_size) {
    uint8_t frame[VSPD_MANAGEMENT_FRAME_MAX];
    vspd_header_t header;
    uint32_t request_id;

    if (client == NULL || client->fd < 0 ||
        payload_size > VSPD_HELLO_PAYLOAD_SIZE) {
        return VSPD_STATUS_INVALID_ARGUMENT;
    }
    request_id = next_request_id(client);
    memset(&header, 0, sizeof(header));
    header.magic = VSPD_MAGIC;
    header.version_major = VSPD_VERSION_MAJOR;
    header.version_minor = VSPD_VERSION_MINOR;
    header.type = type;
    header.header_size = VSPD_HEADER_SIZE;
    header.payload_size = payload_size;
    header.request_id = request_id;
    header.port_id = port_id;
    if (vspd_encode_header(&header, frame) != VSPD_CODEC_OK) {
        return VSPD_STATUS_BACKEND;
    }
    if (payload_size != 0u) {
        if (payload == NULL) {
            return VSPD_STATUS_INVALID_ARGUMENT;
        }
        memcpy(frame + VSPD_HEADER_SIZE, payload, payload_size);
    }
    if (!send_record(client->fd, frame, VSPD_HEADER_SIZE + payload_size)) {
        return VSPD_STATUS_BACKEND;
    }

    for (;;) {
        vspd_header_t response;
        const uint8_t* record_payload;
        ssize_t received;
        bool timed_out = false;
        received = receive_record(client->fd,
                                  frame,
                                  sizeof(frame),
                                  VSPD_MANAGEMENT_TIMEOUT_MS,
                                  &timed_out);
        if (received <= 0) {
            return timed_out ? VSPD_STATUS_TIMEOUT : VSPD_STATUS_BACKEND;
        }
        if (vspd_validate_frame(frame, (size_t)received, &response) !=
            VSPD_CODEC_OK) {
            return VSPD_STATUS_BACKEND;
        }
        record_payload = response.payload_size == 0u
                             ? NULL
                             : frame + VSPD_HEADER_SIZE;
        if ((response.flags & VSPD_FLAG_RESPONSE) == 0u) {
            if (!decode_snapshot_event(client, &response, record_payload)) {
                return VSPD_STATUS_BACKEND;
            }
            continue;
        }
        if (response.type != type || response.request_id != request_id) {
            return VSPD_STATUS_BACKEND;
        }
        if (out_response_size != NULL) {
            *out_response_size = response.payload_size;
        }
        if (response.status != VSPD_STATUS_OK) {
            return response.status;
        }
        if (response.payload_size > response_capacity) {
            return VSPD_STATUS_BUFFER_TOO_SMALL;
        }
        if (response.payload_size != 0u && response_payload != NULL) {
            memcpy(response_payload, record_payload, response.payload_size);
        } else if (response.payload_size != 0u) {
            return VSPD_STATUS_INVALID_ARGUMENT;
        }
        return VSPD_STATUS_OK;
    }
}

int32_t vspd_management_open(vspd_management_client_t* client,
                             const char* socket_path) {
    struct sockaddr_un address;
    uint8_t hello[VSPD_HELLO_PAYLOAD_SIZE] = {
        VSPD_VERSION_MAJOR, VSPD_VERSION_MINOR, 0u, 0u};
    uint8_t response[VSPD_HELLO_PAYLOAD_SIZE];
    uint32_t response_size = 0u;
    size_t path_length;
    int32_t status;

    if (client == NULL || socket_path == NULL || socket_path[0] == '\0') {
        return VSPD_STATUS_INVALID_ARGUMENT;
    }
    memset(client, 0, sizeof(*client));
    client->fd = -1;
    client->next_request_id = 1u;
    path_length = strlen(socket_path);
    if (path_length >= sizeof(address.sun_path)) {
        return VSPD_STATUS_INVALID_ARGUMENT;
    }

    client->fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (client->fd < 0) {
        return VSPD_STATUS_BACKEND;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, socket_path, path_length + 1u);
    if (connect(client->fd,
                (const struct sockaddr*)&address,
                sizeof(address)) != 0) {
        vspd_management_close(client);
        return VSPD_STATUS_BACKEND;
    }

    status = request(client,
                     VSPD_MSG_HELLO,
                     0u,
                     hello,
                     sizeof(hello),
                     response,
                     sizeof(response),
                     &response_size);
    if (status != VSPD_STATUS_OK || response_size != sizeof(hello) ||
        memcmp(response, hello, sizeof(hello)) != 0) {
        vspd_management_close(client);
        return status == VSPD_STATUS_OK ? VSPD_STATUS_BACKEND : status;
    }
    return VSPD_STATUS_OK;
}

void vspd_management_close(vspd_management_client_t* client) {
    if (client == NULL) {
        return;
    }
    if (client->fd >= 0) {
        close(client->fd);
    }
    memset(client, 0, sizeof(*client));
    client->fd = -1;
    client->next_request_id = 1u;
}

int32_t vspd_management_get_server_info(
    vspd_management_client_t* client,
    vspd_server_info_payload_t* out_info) {
    uint8_t payload[VSPD_SERVER_INFO_PAYLOAD_SIZE];
    uint32_t size = 0u;
    int32_t status;
    if (out_info == NULL) {
        return VSPD_STATUS_INVALID_ARGUMENT;
    }
    status = request(client,
                     VSPD_MSG_GET_SERVER_INFO,
                     0u,
                     NULL,
                     0u,
                     payload,
                     sizeof(payload),
                     &size);
    if (status != VSPD_STATUS_OK) {
        return status;
    }
    if (size != sizeof(payload)) {
        return VSPD_STATUS_BACKEND;
    }
    vspd_decode_server_info(payload, out_info);
    return VSPD_STATUS_OK;
}

int32_t vspd_management_get_port_info(
    vspd_management_client_t* client,
    uint32_t port_id,
    vspd_port_info_payload_t* out_info) {
    uint8_t payload[VSPD_PORT_INFO_PAYLOAD_SIZE];
    uint32_t size = 0u;
    int32_t status;
    if (out_info == NULL) {
        return VSPD_STATUS_INVALID_ARGUMENT;
    }
    status = request(client,
                     VSPD_MSG_GET_PORT_INFO,
                     port_id,
                     NULL,
                     0u,
                     payload,
                     sizeof(payload),
                     &size);
    if (status != VSPD_STATUS_OK) {
        return status;
    }
    if (size != sizeof(payload)) {
        return VSPD_STATUS_BACKEND;
    }
    vspd_decode_port_info(payload, out_info);
    return VSPD_STATUS_OK;
}

int32_t vspd_management_get_port_statistics(
    vspd_management_client_t* client,
    uint32_t port_id,
    vspd_statistics_payload_t* out_statistics) {
    uint8_t payload[VSPD_STATISTICS_PAYLOAD_SIZE];
    uint32_t size = 0u;
    int32_t status;
    if (out_statistics == NULL) {
        return VSPD_STATUS_INVALID_ARGUMENT;
    }
    status = request(client,
                     VSPD_MSG_GET_PORT_STATISTICS,
                     port_id,
                     NULL,
                     0u,
                     payload,
                     sizeof(payload),
                     &size);
    if (status != VSPD_STATUS_OK) {
        return status;
    }
    if (size != sizeof(payload)) {
        return VSPD_STATUS_BACKEND;
    }
    vspd_decode_statistics(payload, out_statistics);
    return VSPD_STATUS_OK;
}

int32_t vspd_management_clear_port_statistics(
    vspd_management_client_t* client,
    uint32_t port_id) {
    uint32_t size = 0u;
    int32_t status = request(client,
                             VSPD_MSG_CLEAR_PORT_STATISTICS,
                             port_id,
                             NULL,
                             0u,
                             NULL,
                             0u,
                             &size);
    if (status == VSPD_STATUS_OK && size != 0u) {
        return VSPD_STATUS_BACKEND;
    }
    return status;
}

static int32_t subscription_request(vspd_management_client_t* client,
                                    uint8_t type,
                                    uint32_t port_id) {
    uint32_t size = 0u;
    int32_t status = request(client,
                             type,
                             port_id,
                             NULL,
                             0u,
                             NULL,
                             0u,
                             &size);
    if (status == VSPD_STATUS_OK && size != 0u) {
        return VSPD_STATUS_BACKEND;
    }
    return status;
}

int32_t vspd_management_subscribe_port(vspd_management_client_t* client,
                                       uint32_t port_id) {
    return subscription_request(client, VSPD_MSG_SUBSCRIBE_PORT, port_id);
}

int32_t vspd_management_unsubscribe_port(vspd_management_client_t* client,
                                         uint32_t port_id) {
    return subscription_request(client, VSPD_MSG_UNSUBSCRIBE_PORT, port_id);
}

int32_t vspd_management_receive_snapshot(
    vspd_management_client_t* client,
    int timeout_ms,
    uint32_t* out_port_id,
    vspd_port_snapshot_payload_t* out_snapshot) {
    uint8_t frame[VSPD_MANAGEMENT_FRAME_MAX];
    vspd_header_t header;
    const uint8_t* payload;
    ssize_t received;
    bool timed_out = false;

    if (client == NULL || client->fd < 0 || timeout_ms < -1 ||
        out_port_id == NULL || out_snapshot == NULL) {
        return VSPD_STATUS_INVALID_ARGUMENT;
    }
    if (pop_event(client, out_port_id, out_snapshot)) {
        return VSPD_STATUS_OK;
    }

    received = receive_record(client->fd,
                              frame,
                              sizeof(frame),
                              timeout_ms,
                              &timed_out);
    if (received <= 0) {
        return timed_out ? VSPD_STATUS_TIMEOUT : VSPD_STATUS_BACKEND;
    }
    if (vspd_validate_frame(frame, (size_t)received, &header) != VSPD_CODEC_OK ||
        (header.flags & VSPD_FLAG_RESPONSE) != 0u) {
        return VSPD_STATUS_BACKEND;
    }
    payload = header.payload_size == 0u ? NULL : frame + VSPD_HEADER_SIZE;
    if (!decode_snapshot_event(client, &header, payload) ||
        !pop_event(client, out_port_id, out_snapshot)) {
        return VSPD_STATUS_BACKEND;
    }
    return VSPD_STATUS_OK;
}
''')

# ---------------------------------------------------------------------------
# vspwd subscription state and event emission.
# ---------------------------------------------------------------------------
replace_once(
    "src/vspwd/server.c",
    "    VSPWD_FRAME_MAX = VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD\n};\n",
    "    VSPWD_FRAME_MAX = VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD\n};\n\n_Static_assert(VSPWD_PORT_COUNT <= 32,\n               \"monitor subscription mask supports at most 32 ports\");\n")

replace_once(
    "src/vspwd/server.c",
    "typedef struct vspwd_client {\n    int fd;\n    bool hello_done;\n    int port_id;\n    bool response_pending;\n",
    "typedef struct vspwd_client {\n    int fd;\n    bool hello_done;\n    int port_id;\n    uint32_t monitor_mask;\n    uint32_t monitor_pending_mask;\n    bool response_pending;\n")

replace_once(
    "src/vspwd/server.c",
    "static volatile sig_atomic_t vspwd_stop_requested = 0;\n",
    "static volatile sig_atomic_t vspwd_stop_requested = 0;\n\nstatic void vspwd_mark_port_changed(vspwd_server_t* server, int port_id);\n")

replace_once(
    "src/vspwd/server.c",
    "            port->state = next_state;\n            port->state_event_pending = true;\n",
    "            port->state = next_state;\n            port->state_event_pending = true;\n            vspwd_mark_port_changed(server, port_id);\n")

replace_once(
    "src/vspwd/server.c",
    "static void vspwd_statistics_to_wire(const spw_statistics_t* source,\n                                     vspd_statistics_payload_t* destination) {\n    memset(destination, 0, sizeof(*destination));\n    destination->tx_packets = source->tx_packets;\n    destination->rx_packets = source->rx_packets;\n    destination->tx_bytes = source->tx_bytes;\n    destination->rx_bytes = source->rx_bytes;\n    destination->tx_time_codes = source->tx_time_codes;\n    destination->rx_time_codes = source->rx_time_codes;\n    destination->eep_packets = source->eep_packets;\n    destination->link_errors = source->link_errors;\n    destination->dropped_packets = source->dropped_packets;\n}\n",
    "static void vspwd_statistics_to_wire(const spw_statistics_t* source,\n                                     vspd_statistics_payload_t* destination) {\n    memset(destination, 0, sizeof(*destination));\n    destination->tx_packets = source->tx_packets;\n    destination->rx_packets = source->rx_packets;\n    destination->tx_bytes = source->tx_bytes;\n    destination->rx_bytes = source->rx_bytes;\n    destination->tx_time_codes = source->tx_time_codes;\n    destination->rx_time_codes = source->rx_time_codes;\n    destination->eep_packets = source->eep_packets;\n    destination->link_errors = source->link_errors;\n    destination->dropped_packets = source->dropped_packets;\n}\n\nstatic void vspwd_port_info_to_wire(const vspwd_port_t* port,\n                                    vspd_port_info_payload_t* info) {\n    memset(info, 0, sizeof(*info));\n    if (port->client_index >= 0) {\n        info->flags |= VSPD_PORT_INFO_ATTACHED;\n    }\n    if (port->started) {\n        info->flags |= VSPD_PORT_INFO_STARTED;\n    }\n    if (port->reset_latched) {\n        info->flags |= VSPD_PORT_INFO_RESET_LATCHED;\n    }\n    if (port->ever_attached) {\n        info->flags |= VSPD_PORT_INFO_EVER_ATTACHED;\n    }\n    info->link_state = port->state;\n    info->packet_queue_count = port->packets.count;\n    info->time_code_queue_count = port->time_codes.count;\n}\n\nstatic void vspwd_port_snapshot_to_wire(\n    const vspwd_port_t* port,\n    vspd_port_snapshot_payload_t* snapshot) {\n    memset(snapshot, 0, sizeof(*snapshot));\n    vspwd_port_info_to_wire(port, &snapshot->info);\n    vspwd_statistics_to_wire(&port->statistics, &snapshot->statistics);\n}\n\nstatic void vspwd_mark_port_changed(vspwd_server_t* server, int port_id) {\n    uint32_t bit;\n    int i;\n    if (port_id < 0 || port_id >= VSPWD_PORT_COUNT) {\n        return;\n    }\n    bit = UINT32_C(1) << (uint32_t)port_id;\n    for (i = 0; i < VSPWD_CLIENT_COUNT; ++i) {\n        vspwd_client_t* client = &server->clients[i];\n        if (client->fd >= 0 && client->hello_done && client->port_id < 0 &&\n            (client->monitor_mask & bit) != 0u) {\n            client->monitor_pending_mask |= bit;\n        }\n    }\n}\n")

replace_once(
    "src/vspwd/server.c",
    "            vspwd_clear_port_queues(port);\n        }\n    }\n    client->port_id = -1;\n",
    "            vspwd_clear_port_queues(port);\n            vspwd_mark_port_changed(server, client->port_id);\n        }\n    }\n    client->port_id = -1;\n")

replace_once(
    "src/vspwd/server.c",
    "    if (reassembly->terminator == SPW_TERMINATOR_EEP) {\n        ++source->statistics.eep_packets;\n    }\n    return true;\n}\n",
    "    if (reassembly->terminator == SPW_TERMINATOR_EEP) {\n        ++source->statistics.eep_packets;\n    }\n    vspwd_mark_port_changed(server, source_port_id);\n    vspwd_mark_port_changed(server, vspwd_peer_port(source_port_id));\n    return true;\n}\n")

replace_once(
    "src/vspwd/server.c",
    "    ++source->statistics.tx_time_codes;\n    return true;\n}\n",
    "    ++source->statistics.tx_time_codes;\n    vspwd_mark_port_changed(server, source_port_id);\n    vspwd_mark_port_changed(server, vspwd_peer_port(source_port_id));\n    return true;\n}\n")

replace_once(
    "src/vspwd/server.c",
    "        header.type == VSPD_MSG_TIME_CODE_RX ||\n        header.type == VSPD_MSG_LINK_STATE_EVENT) {\n",
    "        header.type == VSPD_MSG_TIME_CODE_RX ||\n        header.type == VSPD_MSG_LINK_STATE_EVENT ||\n        header.type == VSPD_MSG_PORT_SNAPSHOT_EVENT) {\n")

replace_once(
    "src/vspwd/server.c",
    "                    ++server->ports[client->port_id].statistics.dropped_packets;\n                    status = VSPD_STATUS_RESOURCE_EXHAUSTED;\n",
    "                    ++server->ports[client->port_id].statistics.dropped_packets;\n                    vspwd_mark_port_changed(server, client->port_id);\n                    status = VSPD_STATUS_RESOURCE_EXHAUSTED;\n")

# There are two resource-exhausted dropped-packet sites; patch the remaining one.
replace_once(
    "src/vspwd/server.c",
    "        ++server->ports[client->port_id].statistics.dropped_packets;\n        return VSPD_STATUS_RESOURCE_EXHAUSTED;\n",
    "        ++server->ports[client->port_id].statistics.dropped_packets;\n        vspwd_mark_port_changed(server, client->port_id);\n        return VSPD_STATUS_RESOURCE_EXHAUSTED;\n")

replace_once(
    "src/vspwd/server.c",
    "                memset(&server->ports[client->port_id].statistics,\n                       0,\n                       sizeof(server->ports[client->port_id].statistics));\n            }\n            return vspwd_queue_response(client, &header, status, NULL, 0u);\n\n        case VSPD_MSG_GET_SERVER_INFO: {\n",
    "                memset(&server->ports[client->port_id].statistics,\n                       0,\n                       sizeof(server->ports[client->port_id].statistics));\n                vspwd_mark_port_changed(server, client->port_id);\n            }\n            return vspwd_queue_response(client, &header, status, NULL, 0u);\n\n        case VSPD_MSG_GET_SERVER_INFO: {\n")

replace_once(
    "src/vspwd/server.c",
    "            if (status == VSPD_STATUS_OK) {\n                port = &server->ports[header.port_id];\n                if (port->client_index >= 0) {\n                    info.flags |= VSPD_PORT_INFO_ATTACHED;\n                }\n                if (port->started) {\n                    info.flags |= VSPD_PORT_INFO_STARTED;\n                }\n                if (port->reset_latched) {\n                    info.flags |= VSPD_PORT_INFO_RESET_LATCHED;\n                }\n                if (port->ever_attached) {\n                    info.flags |= VSPD_PORT_INFO_EVER_ATTACHED;\n                }\n                info.link_state = port->state;\n                info.packet_queue_count = port->packets.count;\n                info.time_code_queue_count = port->time_codes.count;\n                vspd_encode_port_info(&info, response_payload);\n            }\n",
    "            if (status == VSPD_STATUS_OK) {\n                port = &server->ports[header.port_id];\n                vspwd_port_info_to_wire(port, &info);\n                vspd_encode_port_info(&info, response_payload);\n            }\n")

replace_once(
    "src/vspwd/server.c",
    "                memset(&server->ports[header.port_id].statistics,\n                       0,\n                       sizeof(server->ports[header.port_id].statistics));\n            }\n            return vspwd_queue_response(client, &header, status, NULL, 0u);\n\n        default:\n",
    "                memset(&server->ports[header.port_id].statistics,\n                       0,\n                       sizeof(server->ports[header.port_id].statistics));\n                vspwd_mark_port_changed(server, (int)header.port_id);\n            }\n            return vspwd_queue_response(client, &header, status, NULL, 0u);\n\n        case VSPD_MSG_SUBSCRIBE_PORT:\n            status = vspwd_validate_management_request(client, &header, true);\n            if (status == VSPD_STATUS_OK) {\n                uint32_t bit = UINT32_C(1) << header.port_id;\n                client->monitor_mask |= bit;\n                client->monitor_pending_mask |= bit;\n            }\n            return vspwd_queue_response(client, &header, status, NULL, 0u);\n\n        case VSPD_MSG_UNSUBSCRIBE_PORT:\n            status = vspwd_validate_management_request(client, &header, true);\n            if (status == VSPD_STATUS_OK) {\n                uint32_t bit = UINT32_C(1) << header.port_id;\n                client->monitor_mask &= ~bit;\n                client->monitor_pending_mask &= ~bit;\n            }\n            return vspwd_queue_response(client, &header, status, NULL, 0u);\n\n        default:\n")

replace_once(
    "src/vspwd/server.c",
    "    ++port->statistics.rx_packets;\n    port->statistics.rx_bytes += slot->length;\n    port->packets.head = (port->packets.head + 1u) % VSPWD_PACKET_QUEUE_DEPTH;\n    --port->packets.count;\n    return true;\n}\n",
    "    ++port->statistics.rx_packets;\n    port->statistics.rx_bytes += slot->length;\n    port->packets.head = (port->packets.head + 1u) % VSPWD_PACKET_QUEUE_DEPTH;\n    --port->packets.count;\n    vspwd_mark_port_changed(server, client->port_id);\n    return true;\n}\n")

replace_once(
    "src/vspwd/server.c",
    "    --port->time_codes.count;\n    ++port->statistics.rx_time_codes;\n    return true;\n}\n",
    "    --port->time_codes.count;\n    ++port->statistics.rx_time_codes;\n    vspwd_mark_port_changed(server, client->port_id);\n    return true;\n}\n")

replace_once(
    "src/vspwd/server.c",
    "static bool vspwd_client_has_output(const vspwd_server_t* server,\n                                    int client_index) {\n",
    "static bool vspwd_flush_monitor_event(vspwd_server_t* server,\n                                      int client_index) {\n    vspwd_client_t* client = &server->clients[client_index];\n    uint8_t frame[VSPD_HEADER_SIZE + VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE];\n    vspd_port_snapshot_payload_t snapshot;\n    vspd_header_t header;\n    uint32_t port_id;\n\n    if (client->port_id >= 0 || client->monitor_pending_mask == 0u) {\n        return true;\n    }\n    for (port_id = 0u; port_id < VSPWD_PORT_COUNT; ++port_id) {\n        uint32_t bit = UINT32_C(1) << port_id;\n        if ((client->monitor_pending_mask & bit) != 0u) {\n            memset(&header, 0, sizeof(header));\n            header.magic = VSPD_MAGIC;\n            header.version_major = VSPD_VERSION_MAJOR;\n            header.version_minor = VSPD_VERSION_MINOR;\n            header.type = VSPD_MSG_PORT_SNAPSHOT_EVENT;\n            header.header_size = VSPD_HEADER_SIZE;\n            header.payload_size = VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE;\n            header.port_id = port_id;\n            if (vspd_encode_header(&header, frame) != VSPD_CODEC_OK) {\n                return false;\n            }\n            vspwd_port_snapshot_to_wire(&server->ports[port_id], &snapshot);\n            vspd_encode_port_snapshot(&snapshot, frame + VSPD_HEADER_SIZE);\n            if (!vspwd_send_record(client->fd, frame, sizeof(frame))) {\n                return false;\n            }\n            client->monitor_pending_mask &= ~bit;\n            return true;\n        }\n    }\n    return true;\n}\n\nstatic bool vspwd_client_has_output(const vspwd_server_t* server,\n                                    int client_index) {\n")

replace_once(
    "src/vspwd/server.c",
    "    if (client->port_id < 0) {\n        return false;\n    }\n",
    "    if (client->port_id < 0) {\n        return client->monitor_pending_mask != 0u;\n    }\n")

replace_once(
    "src/vspwd/server.c",
    "    if (client->response_pending) {\n        return vspwd_flush_response(client);\n    }\n    if (!vspwd_flush_state_event(server, client_index)) {\n",
    "    if (client->response_pending) {\n        return vspwd_flush_response(client);\n    }\n    if (client->port_id < 0) {\n        return vspwd_flush_monitor_event(server, client_index);\n    }\n    if (!vspwd_flush_state_event(server, client_index)) {\n")

# ---------------------------------------------------------------------------
# spwmon CLI.
# ---------------------------------------------------------------------------
write("src/tools/spwmon.c", r'''// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L

#include "tools/vspd_management_client.h"
#include "vspwd/server.h"

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t stop_requested = 0;

static void signal_handler(int signal_number) {
    (void)signal_number;
    stop_requested = 1;
}

static void usage(const char* program) {
    fprintf(stderr,
            "usage: %s [--socket PATH] [--port PORT] [--count N] [--json]\n"
            "\n"
            "Continuously observes vspwd port metadata without ATTACHing to an\n"
            "application port. Without --port, all daemon ports are monitored.\n",
            program);
}

static const char* state_name(uint32_t state) {
    switch (state) {
        case VSPD_LINK_ERROR_RESET: return "ERROR_RESET";
        case VSPD_LINK_ERROR_WAIT: return "ERROR_WAIT";
        case VSPD_LINK_READY: return "READY";
        case VSPD_LINK_STARTED: return "STARTED";
        case VSPD_LINK_CONNECTING: return "CONNECTING";
        case VSPD_LINK_RUN: return "RUN";
        default: return "UNKNOWN";
    }
}

static const char* status_name(int32_t status) {
    switch (status) {
        case VSPD_STATUS_OK: return "ok";
        case VSPD_STATUS_INVALID_ARGUMENT: return "invalid argument";
        case VSPD_STATUS_INVALID_STATE: return "invalid state";
        case VSPD_STATUS_TIMEOUT: return "timeout";
        case VSPD_STATUS_UNSUPPORTED: return "unsupported";
        case VSPD_STATUS_RESOURCE_EXHAUSTED: return "resource exhausted";
        case VSPD_STATUS_LINK_UNAVAILABLE: return "link unavailable";
        case VSPD_STATUS_BUFFER_TOO_SMALL: return "buffer too small";
        case VSPD_STATUS_INVALID_PACKET: return "invalid packet";
        case VSPD_STATUS_BACKEND: return "daemon/transport error";
        default: return "unknown error";
    }
}

static int fail_status(const char* operation, int32_t status) {
    fprintf(stderr,
            "spwmon: %s: %s (%" PRId32 ")\n",
            operation,
            status_name(status),
            status);
    return 1;
}

static int parse_u32(const char* text, uint32_t* out_value) {
    char* end = NULL;
    unsigned long value;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX) {
        return 0;
    }
    *out_value = (uint32_t)value;
    return 1;
}

static int parse_u64(const char* text, uint64_t* out_value) {
    char* end = NULL;
    unsigned long long value;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0') {
        return 0;
    }
    *out_value = (uint64_t)value;
    return 1;
}

static void timestamp_utc(char out[32]) {
    struct timespec now;
    struct tm utc;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0 ||
        gmtime_r(&now.tv_sec, &utc) == NULL) {
        (void)snprintf(out, 32u, "unknown");
        return;
    }
    (void)snprintf(out,
                   32u,
                   "%04d-%02d-%02dT%02d:%02d:%02d.%03ldZ",
                   utc.tm_year + 1900,
                   utc.tm_mon + 1,
                   utc.tm_mday,
                   utc.tm_hour,
                   utc.tm_min,
                   utc.tm_sec,
                   now.tv_nsec / 1000000L);
}

static const char* json_bool(uint32_t flags, uint32_t bit) {
    return (flags & bit) != 0u ? "true" : "false";
}

static void print_snapshot(uint32_t port_id,
                           const vspd_port_snapshot_payload_t* snapshot,
                           const vspd_server_info_payload_t* server,
                           bool json) {
    char timestamp[32];
    timestamp_utc(timestamp);
    if (json) {
        printf("{\"timestamp\":\"%s\",\"port\":%" PRIu32
               ",\"attached\":%s,\"started\":%s,\"reset_latched\":%s"
               ",\"ever_attached\":%s,\"state\":\"%s\""
               ",\"packet_queue\":%" PRIu32 ",\"packet_queue_depth\":%" PRIu32
               ",\"time_code_queue\":%" PRIu32 ",\"time_code_queue_depth\":%" PRIu32
               ",\"tx_packets\":%" PRIu64 ",\"rx_packets\":%" PRIu64
               ",\"tx_bytes\":%" PRIu64 ",\"rx_bytes\":%" PRIu64
               ",\"tx_time_codes\":%" PRIu64 ",\"rx_time_codes\":%" PRIu64
               ",\"eep_packets\":%" PRIu64 ",\"link_errors\":%" PRIu64
               ",\"dropped_packets\":%" PRIu64 "}\n",
               timestamp,
               port_id,
               json_bool(snapshot->info.flags, VSPD_PORT_INFO_ATTACHED),
               json_bool(snapshot->info.flags, VSPD_PORT_INFO_STARTED),
               json_bool(snapshot->info.flags, VSPD_PORT_INFO_RESET_LATCHED),
               json_bool(snapshot->info.flags, VSPD_PORT_INFO_EVER_ATTACHED),
               state_name(snapshot->info.link_state),
               snapshot->info.packet_queue_count,
               server->packet_queue_depth,
               snapshot->info.time_code_queue_count,
               server->time_code_queue_depth,
               snapshot->statistics.tx_packets,
               snapshot->statistics.rx_packets,
               snapshot->statistics.tx_bytes,
               snapshot->statistics.rx_bytes,
               snapshot->statistics.tx_time_codes,
               snapshot->statistics.rx_time_codes,
               snapshot->statistics.eep_packets,
               snapshot->statistics.link_errors,
               snapshot->statistics.dropped_packets);
    } else {
        printf("%s port=%" PRIu32 " state=%s attached=%s started=%s "
               "packets=%" PRIu32 "/%" PRIu32 " timecodes=%" PRIu32 "/%" PRIu32
               " tx_packets=%" PRIu64 " rx_packets=%" PRIu64
               " link_errors=%" PRIu64 " dropped=%" PRIu64 "\n",
               timestamp,
               port_id,
               state_name(snapshot->info.link_state),
               (snapshot->info.flags & VSPD_PORT_INFO_ATTACHED) != 0u ? "yes" : "no",
               (snapshot->info.flags & VSPD_PORT_INFO_STARTED) != 0u ? "yes" : "no",
               snapshot->info.packet_queue_count,
               server->packet_queue_depth,
               snapshot->info.time_code_queue_count,
               server->time_code_queue_depth,
               snapshot->statistics.tx_packets,
               snapshot->statistics.rx_packets,
               snapshot->statistics.link_errors,
               snapshot->statistics.dropped_packets);
    }
    fflush(stdout);
}

int main(int argc, char** argv) {
    const char* socket_path = VSPWD_DEFAULT_SOCKET_PATH;
    uint32_t selected_port = 0u;
    bool port_selected = false;
    bool json = false;
    bool count_set = false;
    uint64_t count_limit = 0u;
    uint64_t emitted = 0u;
    vspd_management_client_t client;
    vspd_server_info_payload_t server;
    struct sigaction action;
    int argument;
    int32_t status;
    uint32_t port_id;

    for (argument = 1; argument < argc; ++argument) {
        if (strcmp(argv[argument], "--socket") == 0) {
            if (++argument >= argc) {
                usage(argv[0]);
                return 2;
            }
            socket_path = argv[argument];
        } else if (strcmp(argv[argument], "--port") == 0) {
            if (++argument >= argc || !parse_u32(argv[argument], &selected_port)) {
                usage(argv[0]);
                return 2;
            }
            port_selected = true;
        } else if (strcmp(argv[argument], "--count") == 0) {
            if (++argument >= argc || !parse_u64(argv[argument], &count_limit)) {
                usage(argv[0]);
                return 2;
            }
            count_set = true;
        } else if (strcmp(argv[argument], "--json") == 0) {
            json = true;
        } else if (strcmp(argv[argument], "--help") == 0 ||
                   strcmp(argv[argument], "-h") == 0) {
            usage(argv[0]);
            return 0;
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (count_set && count_limit == 0u) {
        return 0;
    }

    status = vspd_management_open(&client, socket_path);
    if (status != VSPD_STATUS_OK) {
        return fail_status("connect", status);
    }
    status = vspd_management_get_server_info(&client, &server);
    if (status != VSPD_STATUS_OK) {
        vspd_management_close(&client);
        return fail_status("get server info", status);
    }
    if (port_selected && selected_port >= server.port_count) {
        vspd_management_close(&client);
        fprintf(stderr, "spwmon: port %" PRIu32 " does not exist\n", selected_port);
        return 2;
    }

    memset(&action, 0, sizeof(action));
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    (void)sigaction(SIGINT, &action, NULL);
    (void)sigaction(SIGTERM, &action, NULL);
    stop_requested = 0;

    if (port_selected) {
        status = vspd_management_subscribe_port(&client, selected_port);
        if (status != VSPD_STATUS_OK) {
            vspd_management_close(&client);
            return fail_status("subscribe", status);
        }
    } else {
        for (port_id = 0u; port_id < server.port_count; ++port_id) {
            status = vspd_management_subscribe_port(&client, port_id);
            if (status != VSPD_STATUS_OK) {
                vspd_management_close(&client);
                return fail_status("subscribe", status);
            }
        }
    }

    while (!stop_requested && (!count_set || emitted < count_limit)) {
        vspd_port_snapshot_payload_t snapshot;
        status = vspd_management_receive_snapshot(&client, 500, &port_id, &snapshot);
        if (status == VSPD_STATUS_TIMEOUT) {
            continue;
        }
        if (status != VSPD_STATUS_OK) {
            vspd_management_close(&client);
            return fail_status("receive snapshot", status);
        }
        print_snapshot(port_id, &snapshot, &server, json);
        ++emitted;
    }

    if (port_selected) {
        (void)vspd_management_unsubscribe_port(&client, selected_port);
    } else {
        for (port_id = 0u; port_id < server.port_count; ++port_id) {
            (void)vspd_management_unsubscribe_port(&client, port_id);
        }
    }
    vspd_management_close(&client);
    return 0;
}
''')

# ---------------------------------------------------------------------------
# Build/install surface.
# ---------------------------------------------------------------------------
replace_once(
    "CMakeLists.txt",
    "    set_target_properties(spwctl PROPERTIES\n        C_STANDARD 11\n        C_STANDARD_REQUIRED YES\n        C_EXTENSIONS OFF)\nendif()\n",
    "    set_target_properties(spwctl PROPERTIES\n        C_STANDARD 11\n        C_STANDARD_REQUIRED YES\n        C_EXTENSIONS OFF)\n\n    add_executable(spwmon\n        src/tools/spwmon.c\n        src/tools/vspd_management_client.c)\n    target_link_libraries(spwmon PRIVATE spwkit::spwkit)\n    target_include_directories(spwmon PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)\n    target_compile_features(spwmon PRIVATE c_std_11)\n    set_target_properties(spwmon PROPERTIES\n        C_STANDARD 11\n        C_STANDARD_REQUIRED YES\n        C_EXTENSIONS OFF)\nendif()\n")

replace_once(
    "CMakeLists.txt",
    "if(SPWKIT_BUILD_TOOLS)\n    install(TARGETS spwctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})\nendif()\n",
    "if(SPWKIT_BUILD_TOOLS)\n    install(TARGETS spwctl spwmon RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})\nendif()\n")

# ---------------------------------------------------------------------------
# Protocol tests.
# ---------------------------------------------------------------------------
replace_once(
    "tests/vspw_device_protocol.c",
    "        0x01u, 0x01u, 0x09u, 0x0eu,\n",
    "        0x01u, 0x02u, 0x09u, 0x0eu,\n")

replace_once(
    "tests/vspw_device_protocol.c",
    "    uint8_t frame[VSPD_HEADER_SIZE + VSPD_STATISTICS_PAYLOAD_SIZE];\n    uint8_t payload[VSPD_STATISTICS_PAYLOAD_SIZE];\n",
    "    uint8_t frame[VSPD_HEADER_SIZE + VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE];\n    uint8_t payload[VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE];\n")

replace_once(
    "tests/vspw_device_protocol.c",
    "    vspd_port_info_payload_t decoded_port_info;\n",
    "    vspd_port_info_payload_t decoded_port_info;\n    vspd_port_snapshot_payload_t snapshot;\n    vspd_port_snapshot_payload_t decoded_snapshot;\n")

replace_once(
    "tests/vspw_device_protocol.c",
    "    assert(vspd_validate_frame(\n               frame, VSPD_HEADER_SIZE + VSPD_STATISTICS_PAYLOAD_SIZE, NULL) ==\n           VSPD_CODEC_OK);\n}\n\nstatic void test_malformed_frames(void) {\n",
    "    assert(vspd_validate_frame(\n               frame, VSPD_HEADER_SIZE + VSPD_STATISTICS_PAYLOAD_SIZE, NULL) ==\n           VSPD_CODEC_OK);\n\n    snapshot.info = port_info;\n    snapshot.statistics = statistics;\n    memset(&decoded_snapshot, 0, sizeof(decoded_snapshot));\n    vspd_encode_port_snapshot(&snapshot, payload);\n    vspd_decode_port_snapshot(payload, &decoded_snapshot);\n    assert(decoded_snapshot.info.flags == snapshot.info.flags);\n    assert(decoded_snapshot.info.link_state == snapshot.info.link_state);\n    assert_statistics_equal(&snapshot.statistics, &decoded_snapshot.statistics);\n    header = header_for(VSPD_MSG_PORT_SNAPSHOT_EVENT,\n                        0u,\n                        VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE,\n                        0u,\n                        1u);\n    encode_frame(&header, payload, frame, sizeof(frame));\n    assert(vspd_validate_frame(\n               frame, VSPD_HEADER_SIZE + VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE, NULL) ==\n           VSPD_CODEC_OK);\n\n    header = header_for(VSPD_MSG_SUBSCRIBE_PORT, 0u, 0u, 21u, 1u);\n    encode_frame(&header, NULL, frame, sizeof(frame));\n    assert(vspd_validate_frame(frame, VSPD_HEADER_SIZE, NULL) == VSPD_CODEC_OK);\n}\n\nstatic void test_malformed_frames(void) {\n")

# ---------------------------------------------------------------------------
# Live spwmon integration.
# ---------------------------------------------------------------------------
write("tests/device/run_spwmon.sh", r'''#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 VSPWD SPWMON HOLD_PEER" >&2
  exit 2
fi

vspwd="$1"
spwmon="$2"
hold_peer="$3"
tmpdir="$(mktemp -d)"
socket="$tmpdir/vspwd.sock"
stop0="$tmpdir/stop0"
stop1="$tmpdir/stop1"
stop1b="$tmpdir/stop1b"
daemon_pid=""
p0_pid=""
p1_pid=""
mon_pid=""

cleanup() {
  set +e
  touch "$stop0" "$stop1" "$stop1b" 2>/dev/null || true
  [[ -n "$mon_pid" ]] && kill -TERM "$mon_pid" 2>/dev/null || true
  [[ -n "$mon_pid" ]] && wait "$mon_pid" 2>/dev/null || true
  [[ -n "$p0_pid" ]] && wait "$p0_pid" 2>/dev/null || true
  [[ -n "$p1_pid" ]] && wait "$p1_pid" 2>/dev/null || true
  [[ -n "$daemon_pid" ]] && kill -TERM "$daemon_pid" 2>/dev/null || true
  [[ -n "$daemon_pid" ]] && wait "$daemon_pid" 2>/dev/null || true
  rm -rf "$tmpdir"
}
trap cleanup EXIT

wait_for() {
  local description="$1"
  local command="$2"
  for _ in $(seq 1 200); do
    if eval "$command"; then
      return 0
    fi
    sleep 0.02
  done
  echo "timed out waiting for $description" >&2
  cat "$tmpdir"/*.log 2>/dev/null || true
  return 1
}

"$vspwd" --socket "$socket" >"$tmpdir/vspwd.log" 2>&1 & daemon_pid=$!
wait_for "daemon socket" "[[ -S '$socket' ]]"

"$hold_peer" "$socket" 0 "$stop0" >"$tmpdir/p0.log" 2>&1 & p0_pid=$!
"$hold_peer" "$socket" 1 "$stop1" >"$tmpdir/p1.log" 2>&1 & p1_pid=$!
wait_for "port 0 RUN" "grep -q '^RUN$' '$tmpdir/p0.log'"
wait_for "port 1 RUN" "grep -q '^RUN$' '$tmpdir/p1.log'"

# Bounded mode must emit the immediate subscription snapshot and exit.
"$spwmon" --socket "$socket" --port 0 --count 1 --json >"$tmpdir/once.log" 2>"$tmpdir/once.err"
grep -q '"port":0' "$tmpdir/once.log"
grep -q '"state":"RUN"' "$tmpdir/once.log"
[[ $(wc -l <"$tmpdir/once.log") -eq 1 ]]

# Continuous mode must remain non-owning while the application peer disappears
# and returns. The ERROR_WAIT snapshot also proves statistic changes are pushed:
# vspwd increments link_errors when a started port loses its peer.
"$spwmon" --socket "$socket" --port 0 --json >"$tmpdir/monitor.log" 2>"$tmpdir/monitor.err" & mon_pid=$!
wait_for "initial monitor RUN" "grep -q '\"state\":\"RUN\"' '$tmpdir/monitor.log'"

touch "$stop1"
wait "$p1_pid"
p1_pid=""
wait_for "ERROR_WAIT snapshot" "grep -q '\"state\":\"ERROR_WAIT\"' '$tmpdir/monitor.log'"
grep -Eq '"link_errors":[1-9][0-9]*' "$tmpdir/monitor.log"

"$hold_peer" "$socket" 1 "$stop1b" >"$tmpdir/p1b.log" 2>&1 & p1_pid=$!
wait_for "replacement port 1 RUN" "grep -q '^RUN$' '$tmpdir/p1b.log'"
wait_for "recovered monitor RUN" "[[ $(grep -c '\"state\":\"RUN\"' '$tmpdir/monitor.log') -ge 2 ]]"

# The monitor connection did not steal port 0; its application owner is alive
# for the whole observation interval.
kill -0 "$p0_pid"
kill -TERM "$mon_pid"
wait "$mon_pid"
mon_pid=""

touch "$stop0" "$stop1b"
wait "$p0_pid"
p0_pid=""
wait "$p1_pid"
p1_pid=""
''')

replace_once(
    "tests/device/CMakeLists.txt",
    "        set_tests_properties(spwctl_management_non_owning PROPERTIES\n            LABELS \"device;tools;management;integration;process;c\"\n            TIMEOUT 45)\n",
    "        set_tests_properties(spwctl_management_non_owning PROPERTIES\n            LABELS \"device;tools;management;integration;process;c\"\n            TIMEOUT 45)\n\n        add_test(\n            NAME spwmon_passive_push_observation\n            COMMAND bash\n                    ${CMAKE_CURRENT_SOURCE_DIR}/run_spwmon.sh\n                    $<TARGET_FILE:vspwd>\n                    $<TARGET_FILE:spwmon>\n                    $<TARGET_FILE:spwkit_device_hold_peer>)\n        set_tests_properties(spwmon_passive_push_observation PROPERTIES\n            LABELS \"device;tools;management;monitor;integration;process;c\"\n            TIMEOUT 45)\n")

# ---------------------------------------------------------------------------
# Dedicated tool CI and docs.
# ---------------------------------------------------------------------------
replace_once(
    ".github/workflows/tools.yml",
    "    name: spwctl / ${{ matrix.compiler }}\n",
    "    name: spwctl + spwmon / ${{ matrix.compiler }}\n")

replace_once(
    ".github/workflows/tools.yml",
    "          build-tools/spwctl --help\n          cmake --install build-tools --prefix \"$PWD/install-tools\"\n          test -x \"$PWD/install-tools/bin/vspwd\"\n          test -x \"$PWD/install-tools/bin/spwctl\"\n          \"$PWD/install-tools/bin/vspwd\" --help\n          \"$PWD/install-tools/bin/spwctl\" --help\n",
    "          build-tools/spwctl --help\n          build-tools/spwmon --help\n          cmake --install build-tools --prefix \"$PWD/install-tools\"\n          test -x \"$PWD/install-tools/bin/vspwd\"\n          test -x \"$PWD/install-tools/bin/spwctl\"\n          test -x \"$PWD/install-tools/bin/spwmon\"\n          \"$PWD/install-tools/bin/vspwd\" --help\n          \"$PWD/install-tools/bin/spwctl\" --help\n          \"$PWD/install-tools/bin/spwmon\" --help\n")

replace_once(
    "docs/vspw-device-protocol.md",
    "# VSPD v1.1 — virtual SpaceWire device protocol\n",
    "# VSPD v1.2 — virtual SpaceWire device protocol\n")
replace_once(
    "docs/vspw-device-protocol.md",
    "VSPD v1.1 uses the same fixed 40-byte header:\n",
    "VSPD v1.2 uses the same fixed 40-byte header:\n")
replace_once(
    "docs/vspw-device-protocol.md",
    "| 5 | 1 | version_minor | `1` |\n",
    "| 5 | 1 | version_minor | `2` |\n")
replace_once(
    "docs/vspw-device-protocol.md",
    "| 19 | `CLEAR_PORT_STATISTICS` | non-owning management request/response |\n",
    "| 19 | `CLEAR_PORT_STATISTICS` | non-owning management request/response |\n| 20 | `SUBSCRIBE_PORT` | non-owning management request/response |\n| 21 | `UNSUBSCRIBE_PORT` | non-owning management request/response |\n| 22 | `PORT_SNAPSHOT_EVENT` | asynchronous non-owning monitor event |\n")
replace_once(
    "docs/vspw-device-protocol.md",
    "byte 1  minor = 1\n",
    "byte 1  minor = 2\n")
replace_once(
    "docs/vspw-device-protocol.md",
    "The v1.1 codec currently requires an exact 1.1 match. Version-range negotiation can be introduced in a later protocol revision rather than inferred from native package versions.\n",
    "The v1.2 codec currently requires an exact 1.2 match. Version-range negotiation can be introduced in a later protocol revision rather than inferred from native package versions.\n")
replace_once(
    "docs/vspw-device-protocol.md",
    "VSPD 1.1 adds a separate non-owning management connection used by `spwctl`. A management client performs HELLO but never ATTACHes to a virtual port. It can discover daemon bounds, inspect per-port ownership/link/queue state, read per-port statistics, and clear statistics without displacing the application owner. Lifecycle overrides and topology mutation are deliberately not part of v1.1.\n",
    "VSPD 1.1 added a separate non-owning management connection used by `spwctl`. A management client performs HELLO but never ATTACHes to a virtual port. It can discover daemon bounds, inspect per-port ownership/link/queue state, read per-port statistics, and clear statistics without displacing the application owner. VSPD 1.2 extends the same HELLO-only plane with passive subscriptions used by `spwmon`; lifecycle overrides and topology mutation remain deliberately absent.\n")
replace_once(
    "docs/vspw-device-protocol.md",
    "`GET_PORT_STATISTICS` returns the normal 72-byte statistics payload for the selected port. `CLEAR_PORT_STATISTICS` clears those counters and returns no payload. These operations do not ATTACH, START, STOP, RESET, dequeue traffic, or alter application ownership.\n",
    "`GET_PORT_STATISTICS` returns the normal 72-byte statistics payload for the selected port. `CLEAR_PORT_STATISTICS` clears those counters and returns no payload. These operations do not ATTACH, START, STOP, RESET, dequeue traffic, or alter application ownership.\n\nVSPD 1.2 adds `SUBSCRIBE_PORT` and `UNSUBSCRIBE_PORT` on the same HELLO-only management connection. A successful subscription queues an immediate `PORT_SNAPSHOT_EVENT`, then the daemon emits another snapshot whenever observable metadata changes. The 88-byte snapshot is the 16-byte port-info payload followed by the 72-byte statistics payload. Events are coalesced per subscribed port while pending, so a slow monitor observes the latest bounded snapshot rather than causing an unbounded daemon queue. Snapshot events never contain application DATA payload bytes.\n")

write("docs/spwmon.md", r'''# spwmon

`spwmon` is the passive observation tool for the Linux `vspwd` virtual SpaceWire service.

It connects through the private VSPD management plane, performs HELLO, and subscribes to daemon port snapshots. It **never ATTACHes** to a SpaceWire port, so it cannot displace the application that owns that port.

```text
application -> SPW_BACKEND_DEVICE -> VSPD ATTACH -> vspwd
                                               ^
                                               |
                           HELLO + SUBSCRIBE ---+--- spwmon
```

## Usage

Monitor every daemon port until interrupted:

```bash
spwmon
```

Monitor one port:

```bash
spwmon --port 0
```

Use a non-default daemon endpoint:

```bash
spwmon --socket /tmp/mission-vspwd.sock --port 1
```

Emit JSON Lines for scripts and log ingestion:

```bash
spwmon --json
```

Bound the number of emitted snapshots, which is also useful for tests:

```bash
spwmon --port 0 --count 5 --json
```

`SIGINT` and `SIGTERM` stop continuous monitoring cleanly.

## Snapshot contents

Each event contains only daemon metadata:

- port identity;
- attached, started, reset-latched and ever-attached flags;
- link state;
- packet and time-code queue occupancy;
- TX/RX packet and byte counters;
- TX/RX time-code counters;
- EEP, link-error and dropped-packet counters.

`spwmon` does not receive or print application packet payloads.

## Event behavior

Subscribing to a port immediately queues its current snapshot. Later observable changes mark that port dirty. While an event is still pending, repeated changes are coalesced into the latest snapshot for that port rather than appended to an unbounded queue.

The Unix `SOCK_SEQPACKET` transport is itself bounded as well. If a monitor stops reading, `vspwd` retains pending state and retries when the socket becomes writable; it does not allocate an ever-growing event backlog.

The current daemon tracks at most 32 subscription bits per management connection. The v0.4 reference topology exposes two ports, so this is a deliberately bounded implementation detail rather than a public ABI promise.

## Ownership boundary

Observation is intentionally weaker than administration. `spwmon` cannot:

- ATTACH or DETACH an application port;
- START, STOP or RESET a link;
- mutate topology;
- consume SpaceWire packets or time codes;
- clear statistics.

Use `spwctl` for explicit management queries and statistic clearing. Application lifecycle remains owned by the application using the public `spw_port_*` API.
''')

print("spwmon patch applied")
