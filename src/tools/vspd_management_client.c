// SPDX-License-Identifier: Apache-2.0
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
    VSPD_MANAGEMENT_FRAME_MAX = VSPD_HEADER_SIZE + VSPD_STATISTICS_PAYLOAD_SIZE
};

static bool wait_fd(int fd, short events) {
    struct pollfd descriptor;
    int result;
    descriptor.fd = fd;
    descriptor.events = events;
    descriptor.revents = 0;
    do {
        result = poll(&descriptor, 1u, VSPD_MANAGEMENT_TIMEOUT_MS);
    } while (result < 0 && errno == EINTR);
    return result > 0 &&
           (descriptor.revents & (events | POLLERR | POLLHUP | POLLNVAL)) == events;
}

static bool send_record(int fd, const uint8_t* data, size_t size) {
    ssize_t sent;
    if (!wait_fd(fd, POLLOUT)) {
        return false;
    }
    do {
        sent = send(fd, data, size, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    return sent == (ssize_t)size;
}

static ssize_t receive_record(int fd, uint8_t* frame, size_t capacity) {
    struct iovec iov;
    struct msghdr message;
    ssize_t received;

    if (!wait_fd(fd, POLLIN)) {
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
    vspd_header_t response;
    uint32_t request_id;
    ssize_t received;

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

    received = receive_record(client->fd, frame, sizeof(frame));
    if (received <= 0 ||
        vspd_validate_frame(frame, (size_t)received, &response) != VSPD_CODEC_OK ||
        (response.flags & VSPD_FLAG_RESPONSE) == 0u ||
        response.type != type || response.request_id != request_id) {
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
        memcpy(response_payload, frame + VSPD_HEADER_SIZE, response.payload_size);
    } else if (response.payload_size != 0u) {
        return VSPD_STATUS_INVALID_ARGUMENT;
    }
    return VSPD_STATUS_OK;
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
