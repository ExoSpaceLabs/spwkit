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
# CMake: pure-C Linux tools are optional and separately installable.
# ---------------------------------------------------------------------------
replace_once(
    "CMakeLists.txt",
    '''if(SPWKIT_BUILD_VSPWD AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")\n    message(FATAL_ERROR "SPWKIT_BUILD_VSPWD currently requires Linux")\nendif()\n''',
    '''if(SPWKIT_BUILD_VSPWD AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")\n    message(FATAL_ERROR "SPWKIT_BUILD_VSPWD currently requires Linux")\nendif()\n\nif(SPWKIT_BUILD_TOOLS AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")\n    message(FATAL_ERROR "SPWKIT_BUILD_TOOLS currently requires Linux")\nendif()\n''')

replace_once(
    "CMakeLists.txt",
    '''if(SPWKIT_BUILD_VSPWD)\n    add_executable(vspwd\n        src/vspwd/main.c\n        src/vspwd/server.c)\n    target_link_libraries(vspwd PRIVATE spwkit::spwkit)\n    target_include_directories(vspwd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)\n    target_compile_features(vspwd PRIVATE c_std_11)\n    set_target_properties(vspwd PROPERTIES\n        C_STANDARD 11\n        C_STANDARD_REQUIRED YES\n        C_EXTENSIONS OFF)\nendif()\n''',
    '''if(SPWKIT_BUILD_VSPWD)\n    add_executable(vspwd\n        src/vspwd/main.c\n        src/vspwd/server.c)\n    target_link_libraries(vspwd PRIVATE spwkit::spwkit)\n    target_include_directories(vspwd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)\n    target_compile_features(vspwd PRIVATE c_std_11)\n    set_target_properties(vspwd PROPERTIES\n        C_STANDARD 11\n        C_STANDARD_REQUIRED YES\n        C_EXTENSIONS OFF)\nendif()\n\nif(SPWKIT_BUILD_TOOLS)\n    add_executable(spwctl\n        src/tools/spwctl.c\n        src/tools/vspd_management_client.c)\n    target_link_libraries(spwctl PRIVATE spwkit::spwkit)\n    target_include_directories(spwctl PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)\n    target_compile_features(spwctl PRIVATE c_std_11)\n    set_target_properties(spwctl PROPERTIES\n        C_STANDARD 11\n        C_STANDARD_REQUIRED YES\n        C_EXTENSIONS OFF)\nendif()\n''')

replace_once(
    "CMakeLists.txt",
    '''if(SPWKIT_BUILD_VSPWD)\n    install(TARGETS vspwd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})\nendif()\n''',
    '''if(SPWKIT_BUILD_VSPWD)\n    install(TARGETS vspwd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})\nendif()\nif(SPWKIT_BUILD_TOOLS)\n    install(TARGETS spwctl RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})\nendif()\n''')

# ---------------------------------------------------------------------------
# VSPD v1.1 management-plane wire definitions.
# ---------------------------------------------------------------------------
replace_once(
    "src/backends/device/vspw_device_protocol.h",
    '#define VSPD_VERSION_MINOR 0u\n',
    '#define VSPD_VERSION_MINOR 1u\n')

replace_once(
    "src/backends/device/vspw_device_protocol.h",
    '''#define VSPD_MSG_GET_STATISTICS   13u\n#define VSPD_MSG_CLEAR_STATISTICS 14u\n#define VSPD_MSG_LINK_STATE_EVENT 15u\n''',
    '''#define VSPD_MSG_GET_STATISTICS        13u\n#define VSPD_MSG_CLEAR_STATISTICS      14u\n#define VSPD_MSG_LINK_STATE_EVENT      15u\n#define VSPD_MSG_GET_SERVER_INFO       16u\n#define VSPD_MSG_GET_PORT_INFO         17u\n#define VSPD_MSG_GET_PORT_STATISTICS   18u\n#define VSPD_MSG_CLEAR_PORT_STATISTICS 19u\n''')

replace_once(
    "src/backends/device/vspw_device_protocol.h",
    '''#define VSPD_HELLO_PAYLOAD_SIZE        4u\n#define VSPD_LINK_STATE_PAYLOAD_SIZE   4u\n#define VSPD_CAPABILITIES_PAYLOAD_SIZE 24u\n#define VSPD_TIME_CODE_PAYLOAD_SIZE    2u\n#define VSPD_STATISTICS_PAYLOAD_SIZE   72u\n''',
    '''#define VSPD_HELLO_PAYLOAD_SIZE        4u\n#define VSPD_LINK_STATE_PAYLOAD_SIZE   4u\n#define VSPD_CAPABILITIES_PAYLOAD_SIZE 24u\n#define VSPD_TIME_CODE_PAYLOAD_SIZE    2u\n#define VSPD_STATISTICS_PAYLOAD_SIZE   72u\n#define VSPD_SERVER_INFO_PAYLOAD_SIZE  20u\n#define VSPD_PORT_INFO_PAYLOAD_SIZE    16u\n\n#define VSPD_PORT_INFO_ATTACHED      UINT32_C(0x01)\n#define VSPD_PORT_INFO_STARTED       UINT32_C(0x02)\n#define VSPD_PORT_INFO_RESET_LATCHED UINT32_C(0x04)\n#define VSPD_PORT_INFO_EVER_ATTACHED UINT32_C(0x08)\n#define VSPD_PORT_INFO_KNOWN_MASK    UINT32_C(0x0f)\n''')

replace_once(
    "src/backends/device/vspw_device_protocol.h",
    '''typedef struct vspd_capabilities_payload {\n    uint64_t bits;\n    uint32_t max_packet_size;\n    uint32_t tx_queue_depth;\n    uint32_t rx_queue_depth;\n    uint32_t buffer_alignment;\n} vspd_capabilities_payload_t;\n\ntypedef struct vspd_statistics_payload {\n''',
    '''typedef struct vspd_capabilities_payload {\n    uint64_t bits;\n    uint32_t max_packet_size;\n    uint32_t tx_queue_depth;\n    uint32_t rx_queue_depth;\n    uint32_t buffer_alignment;\n} vspd_capabilities_payload_t;\n\ntypedef struct vspd_server_info_payload {\n    uint32_t port_count;\n    uint32_t client_capacity;\n    uint32_t packet_queue_depth;\n    uint32_t time_code_queue_depth;\n    uint32_t max_logical_packet;\n} vspd_server_info_payload_t;\n\ntypedef struct vspd_port_info_payload {\n    uint32_t flags;\n    uint32_t link_state;\n    uint32_t packet_queue_count;\n    uint32_t time_code_queue_count;\n} vspd_port_info_payload_t;\n\ntypedef struct vspd_statistics_payload {\n''')

replace_once(
    "src/backends/device/vspw_device_protocol.h",
    '''void vspd_encode_capabilities(const vspd_capabilities_payload_t* value,\n                              uint8_t out[VSPD_CAPABILITIES_PAYLOAD_SIZE]);\nvoid vspd_decode_capabilities(const uint8_t in[VSPD_CAPABILITIES_PAYLOAD_SIZE],\n                              vspd_capabilities_payload_t* out);\n\nvoid vspd_encode_statistics(const vspd_statistics_payload_t* value,\n''',
    '''void vspd_encode_capabilities(const vspd_capabilities_payload_t* value,\n                              uint8_t out[VSPD_CAPABILITIES_PAYLOAD_SIZE]);\nvoid vspd_decode_capabilities(const uint8_t in[VSPD_CAPABILITIES_PAYLOAD_SIZE],\n                              vspd_capabilities_payload_t* out);\n\nvoid vspd_encode_server_info(const vspd_server_info_payload_t* value,\n                             uint8_t out[VSPD_SERVER_INFO_PAYLOAD_SIZE]);\nvoid vspd_decode_server_info(const uint8_t in[VSPD_SERVER_INFO_PAYLOAD_SIZE],\n                             vspd_server_info_payload_t* out);\n\nvoid vspd_encode_port_info(const vspd_port_info_payload_t* value,\n                           uint8_t out[VSPD_PORT_INFO_PAYLOAD_SIZE]);\nvoid vspd_decode_port_info(const uint8_t in[VSPD_PORT_INFO_PAYLOAD_SIZE],\n                           vspd_port_info_payload_t* out);\n\nvoid vspd_encode_statistics(const vspd_statistics_payload_t* value,\n''')

# Codec message ranges/shapes.
replace_once(
    "src/backends/device/vspw_device_protocol.c",
    '''static int vspd_type_valid(uint8_t type) {\n    return type >= VSPD_MSG_HELLO && type <= VSPD_MSG_LINK_STATE_EVENT;\n}\n''',
    '''static int vspd_type_valid(uint8_t type) {\n    return type >= VSPD_MSG_HELLO && type <= VSPD_MSG_CLEAR_PORT_STATISTICS;\n}\n''')

replace_once(
    "src/backends/device/vspw_device_protocol.c",
    '''        case VSPD_MSG_GET_STATISTICS:\n            return VSPD_STATISTICS_PAYLOAD_SIZE;\n        default:\n''',
    '''        case VSPD_MSG_GET_STATISTICS:\n        case VSPD_MSG_GET_PORT_STATISTICS:\n            return VSPD_STATISTICS_PAYLOAD_SIZE;\n        case VSPD_MSG_GET_SERVER_INFO:\n            return VSPD_SERVER_INFO_PAYLOAD_SIZE;\n        case VSPD_MSG_GET_PORT_INFO:\n            return VSPD_PORT_INFO_PAYLOAD_SIZE;\n        default:\n''')

replace_once(
    "src/backends/device/vspw_device_protocol.c",
    '''                case VSPD_MSG_GET_STATISTICS:\n                case VSPD_MSG_CLEAR_STATISTICS:\n                    if (header->payload_size != 0u) {\n''',
    '''                case VSPD_MSG_GET_STATISTICS:\n                case VSPD_MSG_CLEAR_STATISTICS:\n                case VSPD_MSG_GET_SERVER_INFO:\n                case VSPD_MSG_GET_PORT_INFO:\n                case VSPD_MSG_GET_PORT_STATISTICS:\n                case VSPD_MSG_CLEAR_PORT_STATISTICS:\n                    if (header->payload_size != 0u) {\n''')

replace_once(
    "src/backends/device/vspw_device_protocol.c",
    '''        if ((header->type == VSPD_MSG_GET_LINK_STATE && response &&\n             header->status == VSPD_STATUS_OK) ||\n            header->type == VSPD_MSG_LINK_STATE_EVENT) {\n            if (header->payload_size == VSPD_LINK_STATE_PAYLOAD_SIZE &&\n                vspd_read_be32(payload) > VSPD_LINK_RUN) {\n                return VSPD_CODEC_INVALID_SHAPE;\n            }\n        }\n''',
    '''        if ((header->type == VSPD_MSG_GET_LINK_STATE && response &&\n             header->status == VSPD_STATUS_OK) ||\n            header->type == VSPD_MSG_LINK_STATE_EVENT) {\n            if (header->payload_size == VSPD_LINK_STATE_PAYLOAD_SIZE &&\n                vspd_read_be32(payload) > VSPD_LINK_RUN) {\n                return VSPD_CODEC_INVALID_SHAPE;\n            }\n        }\n        if (header->type == VSPD_MSG_GET_PORT_INFO && response &&\n            header->status == VSPD_STATUS_OK &&\n            header->payload_size == VSPD_PORT_INFO_PAYLOAD_SIZE) {\n            const uint32_t flags = vspd_read_be32(payload + 0u);\n            const uint32_t state = vspd_read_be32(payload + 4u);\n            if ((flags & ~VSPD_PORT_INFO_KNOWN_MASK) != 0u ||\n                state > VSPD_LINK_RUN) {\n                return VSPD_CODEC_INVALID_SHAPE;\n            }\n        }\n''')

replace_once(
    "src/backends/device/vspw_device_protocol.c",
    '''void vspd_encode_capabilities(const vspd_capabilities_payload_t* value,\n''',
    '''void vspd_encode_server_info(const vspd_server_info_payload_t* value,\n                             uint8_t out[VSPD_SERVER_INFO_PAYLOAD_SIZE]) {\n    if (value == NULL || out == NULL) {\n        return;\n    }\n    vspd_write_be32(out + 0u, value->port_count);\n    vspd_write_be32(out + 4u, value->client_capacity);\n    vspd_write_be32(out + 8u, value->packet_queue_depth);\n    vspd_write_be32(out + 12u, value->time_code_queue_depth);\n    vspd_write_be32(out + 16u, value->max_logical_packet);\n}\n\nvoid vspd_decode_server_info(const uint8_t in[VSPD_SERVER_INFO_PAYLOAD_SIZE],\n                             vspd_server_info_payload_t* out) {\n    if (in == NULL || out == NULL) {\n        return;\n    }\n    out->port_count = vspd_read_be32(in + 0u);\n    out->client_capacity = vspd_read_be32(in + 4u);\n    out->packet_queue_depth = vspd_read_be32(in + 8u);\n    out->time_code_queue_depth = vspd_read_be32(in + 12u);\n    out->max_logical_packet = vspd_read_be32(in + 16u);\n}\n\nvoid vspd_encode_port_info(const vspd_port_info_payload_t* value,\n                           uint8_t out[VSPD_PORT_INFO_PAYLOAD_SIZE]) {\n    if (value == NULL || out == NULL) {\n        return;\n    }\n    vspd_write_be32(out + 0u, value->flags);\n    vspd_write_be32(out + 4u, value->link_state);\n    vspd_write_be32(out + 8u, value->packet_queue_count);\n    vspd_write_be32(out + 12u, value->time_code_queue_count);\n}\n\nvoid vspd_decode_port_info(const uint8_t in[VSPD_PORT_INFO_PAYLOAD_SIZE],\n                           vspd_port_info_payload_t* out) {\n    if (in == NULL || out == NULL) {\n        return;\n    }\n    out->flags = vspd_read_be32(in + 0u);\n    out->link_state = vspd_read_be32(in + 4u);\n    out->packet_queue_count = vspd_read_be32(in + 8u);\n    out->time_code_queue_count = vspd_read_be32(in + 12u);\n}\n\nvoid vspd_encode_capabilities(const vspd_capabilities_payload_t* value,\n''')

# ---------------------------------------------------------------------------
# vspwd: HELLO-only management requests do not acquire application ownership.
# ---------------------------------------------------------------------------
replace_once(
    "src/vspwd/server.c",
    '''static int32_t vspwd_validate_attached_request(const vspwd_client_t* client,\n                                               const vspd_header_t* header) {\n    if (!client->hello_done || client->port_id < 0) {\n        return VSPD_STATUS_INVALID_STATE;\n    }\n    if (header->port_id != (uint32_t)client->port_id) {\n        return VSPD_STATUS_INVALID_ARGUMENT;\n    }\n    return VSPD_STATUS_OK;\n}\n''',
    '''static int32_t vspwd_validate_attached_request(const vspwd_client_t* client,\n                                               const vspd_header_t* header) {\n    if (!client->hello_done || client->port_id < 0) {\n        return VSPD_STATUS_INVALID_STATE;\n    }\n    if (header->port_id != (uint32_t)client->port_id) {\n        return VSPD_STATUS_INVALID_ARGUMENT;\n    }\n    return VSPD_STATUS_OK;\n}\n\nstatic int32_t vspwd_validate_management_request(\n    const vspwd_client_t* client,\n    const vspd_header_t* header,\n    bool requires_port) {\n    if (!client->hello_done || client->port_id >= 0) {\n        return VSPD_STATUS_INVALID_STATE;\n    }\n    if (requires_port && header->port_id >= VSPWD_PORT_COUNT) {\n        return VSPD_STATUS_INVALID_ARGUMENT;\n    }\n    return VSPD_STATUS_OK;\n}\n\nstatic void vspwd_statistics_to_wire(const spw_statistics_t* source,\n                                     vspd_statistics_payload_t* destination) {\n    memset(destination, 0, sizeof(*destination));\n    destination->tx_packets = source->tx_packets;\n    destination->rx_packets = source->rx_packets;\n    destination->tx_bytes = source->tx_bytes;\n    destination->rx_bytes = source->rx_bytes;\n    destination->tx_time_codes = source->tx_time_codes;\n    destination->rx_time_codes = source->rx_time_codes;\n    destination->eep_packets = source->eep_packets;\n    destination->link_errors = source->link_errors;\n    destination->dropped_packets = source->dropped_packets;\n}\n''')

replace_once(
    "src/vspwd/server.c",
    '''        case VSPD_MSG_GET_STATISTICS: {\n            vspd_statistics_payload_t statistics;\n            const spw_statistics_t* source;\n            status = vspwd_validate_attached_request(client, &header);\n            memset(&statistics, 0, sizeof(statistics));\n            if (status == VSPD_STATUS_OK) {\n                source = &server->ports[client->port_id].statistics;\n                statistics.tx_packets = source->tx_packets;\n                statistics.rx_packets = source->rx_packets;\n                statistics.tx_bytes = source->tx_bytes;\n                statistics.rx_bytes = source->rx_bytes;\n                statistics.tx_time_codes = source->tx_time_codes;\n                statistics.rx_time_codes = source->rx_time_codes;\n                statistics.eep_packets = source->eep_packets;\n                statistics.link_errors = source->link_errors;\n                statistics.dropped_packets = source->dropped_packets;\n                vspd_encode_statistics(&statistics, response_payload);\n            }\n            return vspwd_queue_response(client,\n                                        &header,\n                                        status,\n                                        response_payload,\n                                        VSPD_STATISTICS_PAYLOAD_SIZE);\n        }\n''',
    '''        case VSPD_MSG_GET_STATISTICS: {\n            vspd_statistics_payload_t statistics;\n            status = vspwd_validate_attached_request(client, &header);\n            memset(&statistics, 0, sizeof(statistics));\n            if (status == VSPD_STATUS_OK) {\n                vspwd_statistics_to_wire(\n                    &server->ports[client->port_id].statistics, &statistics);\n                vspd_encode_statistics(&statistics, response_payload);\n            }\n            return vspwd_queue_response(client,\n                                        &header,\n                                        status,\n                                        response_payload,\n                                        VSPD_STATISTICS_PAYLOAD_SIZE);\n        }\n''')

replace_once(
    "src/vspwd/server.c",
    '''        case VSPD_MSG_CLEAR_STATISTICS:\n            status = vspwd_validate_attached_request(client, &header);\n            if (status == VSPD_STATUS_OK) {\n                memset(&server->ports[client->port_id].statistics,\n                       0,\n                       sizeof(server->ports[client->port_id].statistics));\n            }\n            return vspwd_queue_response(client, &header, status, NULL, 0u);\n\n        default:\n''',
    '''        case VSPD_MSG_CLEAR_STATISTICS:\n            status = vspwd_validate_attached_request(client, &header);\n            if (status == VSPD_STATUS_OK) {\n                memset(&server->ports[client->port_id].statistics,\n                       0,\n                       sizeof(server->ports[client->port_id].statistics));\n            }\n            return vspwd_queue_response(client, &header, status, NULL, 0u);\n\n        case VSPD_MSG_GET_SERVER_INFO: {\n            vspd_server_info_payload_t info;\n            status = vspwd_validate_management_request(client, &header, false);\n            memset(&info, 0, sizeof(info));\n            if (status == VSPD_STATUS_OK) {\n                info.port_count = VSPWD_PORT_COUNT;\n                info.client_capacity = VSPWD_CLIENT_COUNT;\n                info.packet_queue_depth = VSPWD_PACKET_QUEUE_DEPTH;\n                info.time_code_queue_depth = VSPWD_TIME_CODE_QUEUE_DEPTH;\n                info.max_logical_packet = VSPD_MAX_LOGICAL_PACKET;\n                vspd_encode_server_info(&info, response_payload);\n            }\n            return vspwd_queue_response(client,\n                                        &header,\n                                        status,\n                                        response_payload,\n                                        VSPD_SERVER_INFO_PAYLOAD_SIZE);\n        }\n\n        case VSPD_MSG_GET_PORT_INFO: {\n            vspd_port_info_payload_t info;\n            const vspwd_port_t* port = NULL;\n            status = vspwd_validate_management_request(client, &header, true);\n            memset(&info, 0, sizeof(info));\n            if (status == VSPD_STATUS_OK) {\n                port = &server->ports[header.port_id];\n                if (port->client_index >= 0) {\n                    info.flags |= VSPD_PORT_INFO_ATTACHED;\n                }\n                if (port->started) {\n                    info.flags |= VSPD_PORT_INFO_STARTED;\n                }\n                if (port->reset_latched) {\n                    info.flags |= VSPD_PORT_INFO_RESET_LATCHED;\n                }\n                if (port->ever_attached) {\n                    info.flags |= VSPD_PORT_INFO_EVER_ATTACHED;\n                }\n                info.link_state = port->state;\n                info.packet_queue_count = port->packets.count;\n                info.time_code_queue_count = port->time_codes.count;\n                vspd_encode_port_info(&info, response_payload);\n            }\n            return vspwd_queue_response(client,\n                                        &header,\n                                        status,\n                                        response_payload,\n                                        VSPD_PORT_INFO_PAYLOAD_SIZE);\n        }\n\n        case VSPD_MSG_GET_PORT_STATISTICS: {\n            vspd_statistics_payload_t statistics;\n            status = vspwd_validate_management_request(client, &header, true);\n            memset(&statistics, 0, sizeof(statistics));\n            if (status == VSPD_STATUS_OK) {\n                vspwd_statistics_to_wire(\n                    &server->ports[header.port_id].statistics, &statistics);\n                vspd_encode_statistics(&statistics, response_payload);\n            }\n            return vspwd_queue_response(client,\n                                        &header,\n                                        status,\n                                        response_payload,\n                                        VSPD_STATISTICS_PAYLOAD_SIZE);\n        }\n\n        case VSPD_MSG_CLEAR_PORT_STATISTICS:\n            status = vspwd_validate_management_request(client, &header, true);\n            if (status == VSPD_STATUS_OK) {\n                memset(&server->ports[header.port_id].statistics,\n                       0,\n                       sizeof(server->ports[header.port_id].statistics));\n            }\n            return vspwd_queue_response(client, &header, status, NULL, 0u);\n\n        default:\n''')

# ---------------------------------------------------------------------------
# Private reusable management client for spwctl/spwmon.
# ---------------------------------------------------------------------------
write("src/tools/vspd_management_client.h", r'''// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_VSPD_MANAGEMENT_CLIENT_H
#define SPWKIT_VSPD_MANAGEMENT_CLIENT_H

#include "backends/device/vspw_device_protocol.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vspd_management_client {
    int fd;
    uint32_t next_request_id;
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
''')

write("src/tools/spwctl.c", r'''// SPDX-License-Identifier: Apache-2.0

#include "tools/vspd_management_client.h"
#include "vspwd/server.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char* program) {
    fprintf(stderr,
            "usage: %s [--socket PATH] list\n"
            "       %s [--socket PATH] show PORT\n"
            "       %s [--socket PATH] stats PORT\n"
            "       %s [--socket PATH] clear-stats PORT\n",
            program, program, program, program);
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

static const char* yes_no(uint32_t flags, uint32_t bit) {
    return (flags & bit) != 0u ? "yes" : "no";
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
    fprintf(stderr, "spwctl: %s: %s (%" PRId32 ")\n",
            operation, status_name(status), status);
    return 1;
}

static int parse_port(const char* text, uint32_t* out_port) {
    char* end = NULL;
    unsigned long value;
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX) {
        return 0;
    }
    *out_port = (uint32_t)value;
    return 1;
}

static int command_list(vspd_management_client_t* client) {
    vspd_server_info_payload_t server;
    uint32_t port_id;
    int32_t status = vspd_management_get_server_info(client, &server);
    if (status != VSPD_STATUS_OK) {
        return fail_status("get server info", status);
    }
    printf("PORT ATTACHED STARTED RESET STATE PACKETS TIMECODES\n");
    for (port_id = 0u; port_id < server.port_count; ++port_id) {
        vspd_port_info_payload_t info;
        status = vspd_management_get_port_info(client, port_id, &info);
        if (status != VSPD_STATUS_OK) {
            return fail_status("get port info", status);
        }
        printf("%" PRIu32 " %s %s %s %s %" PRIu32 "/%" PRIu32
               " %" PRIu32 "/%" PRIu32 "\n",
               port_id,
               yes_no(info.flags, VSPD_PORT_INFO_ATTACHED),
               yes_no(info.flags, VSPD_PORT_INFO_STARTED),
               yes_no(info.flags, VSPD_PORT_INFO_RESET_LATCHED),
               state_name(info.link_state),
               info.packet_queue_count,
               server.packet_queue_depth,
               info.time_code_queue_count,
               server.time_code_queue_depth);
    }
    return 0;
}

static int command_show(vspd_management_client_t* client, uint32_t port_id) {
    vspd_server_info_payload_t server;
    vspd_port_info_payload_t info;
    int32_t status = vspd_management_get_server_info(client, &server);
    if (status != VSPD_STATUS_OK) {
        return fail_status("get server info", status);
    }
    status = vspd_management_get_port_info(client, port_id, &info);
    if (status != VSPD_STATUS_OK) {
        return fail_status("get port info", status);
    }
    printf("port: %" PRIu32 "\n", port_id);
    printf("attached: %s\n", yes_no(info.flags, VSPD_PORT_INFO_ATTACHED));
    printf("started: %s\n", yes_no(info.flags, VSPD_PORT_INFO_STARTED));
    printf("reset_latched: %s\n",
           yes_no(info.flags, VSPD_PORT_INFO_RESET_LATCHED));
    printf("ever_attached: %s\n",
           yes_no(info.flags, VSPD_PORT_INFO_EVER_ATTACHED));
    printf("state: %s\n", state_name(info.link_state));
    printf("packet_queue: %" PRIu32 "/%" PRIu32 "\n",
           info.packet_queue_count, server.packet_queue_depth);
    printf("time_code_queue: %" PRIu32 "/%" PRIu32 "\n",
           info.time_code_queue_count, server.time_code_queue_depth);
    printf("max_logical_packet: %" PRIu32 "\n", server.max_logical_packet);
    return 0;
}

static int command_stats(vspd_management_client_t* client, uint32_t port_id) {
    vspd_statistics_payload_t stats;
    int32_t status = vspd_management_get_port_statistics(client, port_id, &stats);
    if (status != VSPD_STATUS_OK) {
        return fail_status("get port statistics", status);
    }
    printf("port: %" PRIu32 "\n", port_id);
    printf("tx_packets: %" PRIu64 "\n", stats.tx_packets);
    printf("rx_packets: %" PRIu64 "\n", stats.rx_packets);
    printf("tx_bytes: %" PRIu64 "\n", stats.tx_bytes);
    printf("rx_bytes: %" PRIu64 "\n", stats.rx_bytes);
    printf("tx_time_codes: %" PRIu64 "\n", stats.tx_time_codes);
    printf("rx_time_codes: %" PRIu64 "\n", stats.rx_time_codes);
    printf("eep_packets: %" PRIu64 "\n", stats.eep_packets);
    printf("link_errors: %" PRIu64 "\n", stats.link_errors);
    printf("dropped_packets: %" PRIu64 "\n", stats.dropped_packets);
    return 0;
}

int main(int argc, char** argv) {
    const char* socket_path = VSPWD_DEFAULT_SOCKET_PATH;
    const char* command;
    uint32_t port_id = 0u;
    vspd_management_client_t client;
    int32_t status;
    int argument = 1;
    int result;

    if (argument < argc && strcmp(argv[argument], "--socket") == 0) {
        if (argument + 1 >= argc) {
            usage(argv[0]);
            return 2;
        }
        socket_path = argv[argument + 1];
        argument += 2;
    }
    if (argument < argc &&
        (strcmp(argv[argument], "--help") == 0 ||
         strcmp(argv[argument], "-h") == 0)) {
        usage(argv[0]);
        return 0;
    }
    if (argument >= argc) {
        usage(argv[0]);
        return 2;
    }
    command = argv[argument++];

    if (strcmp(command, "list") == 0) {
        if (argument != argc) {
            usage(argv[0]);
            return 2;
        }
    } else if (strcmp(command, "show") == 0 ||
               strcmp(command, "stats") == 0 ||
               strcmp(command, "clear-stats") == 0) {
        if (argument + 1 != argc || !parse_port(argv[argument], &port_id)) {
            usage(argv[0]);
            return 2;
        }
    } else {
        usage(argv[0]);
        return 2;
    }

    status = vspd_management_open(&client, socket_path);
    if (status != VSPD_STATUS_OK) {
        return fail_status("connect", status);
    }

    if (strcmp(command, "list") == 0) {
        result = command_list(&client);
    } else if (strcmp(command, "show") == 0) {
        result = command_show(&client, port_id);
    } else if (strcmp(command, "stats") == 0) {
        result = command_stats(&client, port_id);
    } else {
        status = vspd_management_clear_port_statistics(&client, port_id);
        if (status != VSPD_STATUS_OK) {
            result = fail_status("clear port statistics", status);
        } else {
            printf("cleared statistics for port %" PRIu32 "\n", port_id);
            result = 0;
        }
    }

    vspd_management_close(&client);
    return result;
}
''')

# ---------------------------------------------------------------------------
# Tests: protocol codec + live non-owning CLI with application-owned ports.
# ---------------------------------------------------------------------------
replace_once(
    "tests/vspw_device_protocol.c",
    '''static void test_control_payloads(void) {\n    uint8_t frame[VSPD_HEADER_SIZE + VSPD_STATISTICS_PAYLOAD_SIZE];\n    uint8_t payload[VSPD_STATISTICS_PAYLOAD_SIZE];\n''',
    '''static void test_control_payloads(void) {\n    uint8_t frame[VSPD_HEADER_SIZE + VSPD_STATISTICS_PAYLOAD_SIZE];\n    uint8_t payload[VSPD_STATISTICS_PAYLOAD_SIZE];\n''')

replace_once(
    "tests/vspw_device_protocol.c",
    '''    vspd_statistics_payload_t statistics = {\n        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u};\n    vspd_statistics_payload_t decoded_statistics;\n\n    vspd_encode_capabilities(&capabilities, payload);\n''',
    '''    vspd_statistics_payload_t statistics = {\n        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u};\n    vspd_statistics_payload_t decoded_statistics;\n    vspd_server_info_payload_t server_info = {2u, 4u, 2u, 8u, 1048576u};\n    vspd_server_info_payload_t decoded_server_info;\n    vspd_port_info_payload_t port_info = {\n        VSPD_PORT_INFO_ATTACHED | VSPD_PORT_INFO_STARTED |\n            VSPD_PORT_INFO_EVER_ATTACHED,\n        VSPD_LINK_RUN, 1u, 2u};\n    vspd_port_info_payload_t decoded_port_info;\n\n    vspd_encode_capabilities(&capabilities, payload);\n''')

replace_once(
    "tests/vspw_device_protocol.c",
    '''    assert(vspd_decode_u32_payload(payload) == VSPD_LINK_RUN);\n}\n''',
    '''    assert(vspd_decode_u32_payload(payload) == VSPD_LINK_RUN);\n\n    vspd_encode_server_info(&server_info, payload);\n    memset(&decoded_server_info, 0, sizeof(decoded_server_info));\n    vspd_decode_server_info(payload, &decoded_server_info);\n    assert(decoded_server_info.port_count == server_info.port_count);\n    assert(decoded_server_info.client_capacity == server_info.client_capacity);\n    assert(decoded_server_info.packet_queue_depth == server_info.packet_queue_depth);\n    assert(decoded_server_info.time_code_queue_depth == server_info.time_code_queue_depth);\n    assert(decoded_server_info.max_logical_packet == server_info.max_logical_packet);\n    header = header_for(VSPD_MSG_GET_SERVER_INFO,\n                        VSPD_FLAG_RESPONSE,\n                        VSPD_SERVER_INFO_PAYLOAD_SIZE,\n                        17u,\n                        0u);\n    encode_frame(&header, payload, frame, sizeof(frame));\n    assert(vspd_validate_frame(\n               frame, VSPD_HEADER_SIZE + VSPD_SERVER_INFO_PAYLOAD_SIZE, NULL) ==\n           VSPD_CODEC_OK);\n\n    vspd_encode_port_info(&port_info, payload);\n    memset(&decoded_port_info, 0, sizeof(decoded_port_info));\n    vspd_decode_port_info(payload, &decoded_port_info);\n    assert(decoded_port_info.flags == port_info.flags);\n    assert(decoded_port_info.link_state == port_info.link_state);\n    assert(decoded_port_info.packet_queue_count == port_info.packet_queue_count);\n    assert(decoded_port_info.time_code_queue_count == port_info.time_code_queue_count);\n    header = header_for(VSPD_MSG_GET_PORT_INFO,\n                        VSPD_FLAG_RESPONSE,\n                        VSPD_PORT_INFO_PAYLOAD_SIZE,\n                        18u,\n                        1u);\n    encode_frame(&header, payload, frame, sizeof(frame));\n    assert(vspd_validate_frame(\n               frame, VSPD_HEADER_SIZE + VSPD_PORT_INFO_PAYLOAD_SIZE, NULL) ==\n           VSPD_CODEC_OK);\n\n    header = header_for(VSPD_MSG_GET_PORT_STATISTICS,\n                        VSPD_FLAG_RESPONSE,\n                        VSPD_STATISTICS_PAYLOAD_SIZE,\n                        19u,\n                        1u);\n    vspd_encode_statistics(&statistics, payload);\n    encode_frame(&header, payload, frame, sizeof(frame));\n    assert(vspd_validate_frame(\n               frame, VSPD_HEADER_SIZE + VSPD_STATISTICS_PAYLOAD_SIZE, NULL) ==\n           VSPD_CODEC_OK);\n}\n''')

replace_once(
    "tests/vspw_device_protocol.c",
    '''    header = header_for(VSPD_MSG_LINK_STATE_EVENT, 0u, 4u, 0u, 1u);\n    vspd_encode_u32_payload(VSPD_LINK_RUN + 1u, frame + VSPD_HEADER_SIZE);\n    assert(vspd_encode_header(&header, frame) == VSPD_CODEC_OK);\n    assert(vspd_validate_frame(frame, VSPD_HEADER_SIZE + 4u, NULL) ==\n           VSPD_CODEC_INVALID_SHAPE);\n}\n''',
    '''    header = header_for(VSPD_MSG_LINK_STATE_EVENT, 0u, 4u, 0u, 1u);\n    vspd_encode_u32_payload(VSPD_LINK_RUN + 1u, frame + VSPD_HEADER_SIZE);\n    assert(vspd_encode_header(&header, frame) == VSPD_CODEC_OK);\n    assert(vspd_validate_frame(frame, VSPD_HEADER_SIZE + 4u, NULL) ==\n           VSPD_CODEC_INVALID_SHAPE);\n\n    {\n        uint8_t port_payload[VSPD_PORT_INFO_PAYLOAD_SIZE];\n        vspd_port_info_payload_t invalid_port_info = {\n            VSPD_PORT_INFO_KNOWN_MASK + 1u, VSPD_LINK_RUN, 0u, 0u};\n        header = header_for(VSPD_MSG_GET_PORT_INFO,\n                            VSPD_FLAG_RESPONSE,\n                            VSPD_PORT_INFO_PAYLOAD_SIZE,\n                            20u,\n                            0u);\n        vspd_encode_port_info(&invalid_port_info, port_payload);\n        assert(vspd_encode_header(&header, frame) == VSPD_CODEC_OK);\n        memcpy(frame + VSPD_HEADER_SIZE, port_payload, sizeof(port_payload));\n        assert(vspd_validate_frame(\n                   frame, VSPD_HEADER_SIZE + sizeof(port_payload), NULL) ==\n               VSPD_CODEC_INVALID_SHAPE);\n    }\n}\n''')

write("tests/device_hold_peer.c", r'''// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L

#include <spwkit/device.h>
#include <spwkit/spwkit.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int64_t monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return -1;
    }
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void sleep_ms(long milliseconds) {
    struct timespec delay;
    delay.tv_sec = milliseconds / 1000;
    delay.tv_nsec = (milliseconds % 1000) * 1000 * 1000;
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
}

int main(int argc, char** argv) {
    spw_device_config_t device;
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);
    spw_port_t* port = NULL;
    unsigned long parsed_port;
    char* end = NULL;
    int64_t deadline;

    if (argc != 4) {
        fprintf(stderr, "usage: %s SOCKET PORT STOP_FILE\n", argv[0]);
        return 2;
    }
    parsed_port = strtoul(argv[2], &end, 10);
    if (end == argv[2] || *end != '\0' || parsed_port > UINT32_MAX) {
        return 2;
    }

    device = (spw_device_config_t)SPW_DEVICE_CONFIG_INITIALIZER((uint32_t)parsed_port);
    if (snprintf(device.endpoint, sizeof(device.endpoint), "%s", argv[1]) < 0 ||
        strlen(argv[1]) >= sizeof(device.endpoint)) {
        return 2;
    }
    config.backend_config = &device;
    config.backend_config_size = sizeof(device);

    if (spw_port_open(&config, &port) != SPW_OK || port == NULL ||
        spw_port_start(port) != SPW_OK) {
        if (port != NULL) {
            (void)spw_port_close(port);
        }
        return 1;
    }

    deadline = monotonic_ms() + 5000;
    while (monotonic_ms() < deadline) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(port, &state) == SPW_OK &&
            state == SPW_LINK_RUN) {
            printf("RUN\n");
            fflush(stdout);
            break;
        }
        sleep_ms(20);
    }
    {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(port, &state) != SPW_OK ||
            state != SPW_LINK_RUN) {
            (void)spw_port_close(port);
            return 1;
        }
    }

    deadline = monotonic_ms() + 20000;
    while (access(argv[3], F_OK) != 0 && monotonic_ms() < deadline) {
        sleep_ms(20);
    }
    if (access(argv[3], F_OK) != 0) {
        (void)spw_port_close(port);
        return 1;
    }

    return spw_port_close(port) == SPW_OK ? 0 : 1;
}
''')

write("tests/device/run_spwctl.sh", r'''#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "usage: $0 VSPWD SPWCTL HOLD_PEER" >&2
  exit 2
fi

vspwd=$1
spwctl=$2
peer=$3
tmpdir=$(mktemp -d)
socket="$tmpdir/vspwd.sock"
stop_file="$tmpdir/stop"
daemon_pid=""
peer0_pid=""
peer1_pid=""

cleanup() {
  set +e
  touch "$stop_file" 2>/dev/null || true
  [[ -n "$peer0_pid" ]] && kill "$peer0_pid" 2>/dev/null || true
  [[ -n "$peer1_pid" ]] && kill "$peer1_pid" 2>/dev/null || true
  [[ -n "$peer0_pid" ]] && wait "$peer0_pid" 2>/dev/null || true
  [[ -n "$peer1_pid" ]] && wait "$peer1_pid" 2>/dev/null || true
  [[ -n "$daemon_pid" ]] && kill -TERM "$daemon_pid" 2>/dev/null || true
  [[ -n "$daemon_pid" ]] && wait "$daemon_pid" 2>/dev/null || true
  rm -rf "$tmpdir"
}
trap cleanup EXIT

"$vspwd" --socket "$socket" >"$tmpdir/vspwd.log" 2>&1 &
daemon_pid=$!
for _ in $(seq 1 100); do
  [[ -S "$socket" ]] && break
  sleep 0.02
done
[[ -S "$socket" ]] || { cat "$tmpdir/vspwd.log"; exit 1; }

"$spwctl" --socket "$socket" list >"$tmpdir/list-empty.txt"
grep -Eq '^0 no no no ERROR_RESET 0/2 0/8$' "$tmpdir/list-empty.txt"
grep -Eq '^1 no no no ERROR_RESET 0/2 0/8$' "$tmpdir/list-empty.txt"

if "$spwctl" --socket "$socket" show 99 >"$tmpdir/invalid.out" 2>"$tmpdir/invalid.err"; then
  echo "invalid management port unexpectedly succeeded" >&2
  exit 1
fi
grep -q 'invalid argument' "$tmpdir/invalid.err"

"$peer" "$socket" 0 "$stop_file" >"$tmpdir/peer0.log" 2>&1 &
peer0_pid=$!
"$peer" "$socket" 1 "$stop_file" >"$tmpdir/peer1.log" 2>&1 &
peer1_pid=$!
for _ in $(seq 1 250); do
  if grep -q '^RUN$' "$tmpdir/peer0.log" 2>/dev/null &&
     grep -q '^RUN$' "$tmpdir/peer1.log" 2>/dev/null; then
    break
  fi
  kill -0 "$peer0_pid" 2>/dev/null || { cat "$tmpdir/peer0.log"; exit 1; }
  kill -0 "$peer1_pid" 2>/dev/null || { cat "$tmpdir/peer1.log"; exit 1; }
  sleep 0.02
done
grep -q '^RUN$' "$tmpdir/peer0.log"
grep -q '^RUN$' "$tmpdir/peer1.log"

"$spwctl" --socket "$socket" list >"$tmpdir/list-run.txt"
grep -Eq '^0 yes yes no RUN [0-9]+/2 [0-9]+/8$' "$tmpdir/list-run.txt"
grep -Eq '^1 yes yes no RUN [0-9]+/2 [0-9]+/8$' "$tmpdir/list-run.txt"

"$spwctl" --socket "$socket" show 0 >"$tmpdir/show.txt"
grep -q '^attached: yes$' "$tmpdir/show.txt"
grep -q '^started: yes$' "$tmpdir/show.txt"
grep -q '^state: RUN$' "$tmpdir/show.txt"

"$spwctl" --socket "$socket" stats 0 >"$tmpdir/stats.txt"
grep -q '^tx_packets: ' "$tmpdir/stats.txt"
grep -q '^dropped_packets: ' "$tmpdir/stats.txt"
"$spwctl" --socket "$socket" clear-stats 0 >"$tmpdir/clear.txt"
grep -q '^cleared statistics for port 0$' "$tmpdir/clear.txt"
"$spwctl" --socket "$socket" stats 0 >"$tmpdir/stats-cleared.txt"
grep -q '^tx_packets: 0$' "$tmpdir/stats-cleared.txt"
grep -q '^link_errors: 0$' "$tmpdir/stats-cleared.txt"

# Management operations must not steal or alter application ownership/lifecycle.
"$spwctl" --socket "$socket" show 0 >"$tmpdir/show-after.txt"
grep -q '^attached: yes$' "$tmpdir/show-after.txt"
grep -q '^started: yes$' "$tmpdir/show-after.txt"
grep -q '^state: RUN$' "$tmpdir/show-after.txt"

touch "$stop_file"
wait "$peer0_pid"
peer0_pid=""
wait "$peer1_pid"
peer1_pid=""

"$spwctl" --socket "$socket" list >"$tmpdir/list-detached.txt"
grep -Eq '^0 no no no ERROR_RESET 0/2 0/8$' "$tmpdir/list-detached.txt"
grep -Eq '^1 no no no ERROR_RESET 0/2 0/8$' "$tmpdir/list-detached.txt"
''')

replace_once(
    "tests/device/CMakeLists.txt",
    '''if(SPWKIT_DEVICE_RUNTIME_SUPPORTED)\n    add_executable(spwkit_device_public_peer ../device_public_peer.c)\n''',
    '''if(SPWKIT_DEVICE_RUNTIME_SUPPORTED)\n    add_executable(spwkit_device_public_peer ../device_public_peer.c)\n''')

replace_once(
    "tests/device/CMakeLists.txt",
    '''    set_target_properties(spwkit_device_public_peer PROPERTIES\n        C_STANDARD 11\n        C_STANDARD_REQUIRED YES\n        C_EXTENSIONS OFF)\n\n    if(SPWKIT_BUILD_CPP_TESTS AND SPWKIT_ENABLE_HEAP)\n''',
    '''    set_target_properties(spwkit_device_public_peer PROPERTIES\n        C_STANDARD 11\n        C_STANDARD_REQUIRED YES\n        C_EXTENSIONS OFF)\n\n    if(SPWKIT_BUILD_TOOLS AND SPWKIT_ENABLE_HEAP)\n        add_executable(spwkit_device_hold_peer ../device_hold_peer.c)\n        target_link_libraries(spwkit_device_hold_peer PRIVATE spwkit::spwkit)\n        target_compile_features(spwkit_device_hold_peer PRIVATE c_std_11)\n        set_target_properties(spwkit_device_hold_peer PROPERTIES\n            C_STANDARD 11\n            C_STANDARD_REQUIRED YES\n            C_EXTENSIONS OFF)\n    endif()\n\n    if(SPWKIT_BUILD_CPP_TESTS AND SPWKIT_ENABLE_HEAP)\n''')

replace_once(
    "tests/device/CMakeLists.txt",
    '''    if(SPWKIT_BUILD_CPP_TESTS AND SPWKIT_ENABLE_HEAP)\n        add_test(\n            NAME backend_contract_device\n''',
    '''    if(SPWKIT_BUILD_TOOLS AND SPWKIT_ENABLE_HEAP)\n        add_test(\n            NAME spwctl_management_non_owning\n            COMMAND bash\n                    ${CMAKE_CURRENT_SOURCE_DIR}/run_spwctl.sh\n                    $<TARGET_FILE:vspwd>\n                    $<TARGET_FILE:spwctl>\n                    $<TARGET_FILE:spwkit_device_hold_peer>)\n        set_tests_properties(spwctl_management_non_owning PROPERTIES\n            LABELS "device;tools;management;integration;process;c"\n            TIMEOUT 45)\n    endif()\n\n    if(SPWKIT_BUILD_CPP_TESTS AND SPWKIT_ENABLE_HEAP)\n        add_test(\n            NAME backend_contract_device\n''')

# ---------------------------------------------------------------------------
# Documentation.
# ---------------------------------------------------------------------------
replace_once(
    "docs/vspw-device-protocol.md",
    '# VSPD v1 — virtual SpaceWire device protocol\n',
    '# VSPD v1.1 — virtual SpaceWire device protocol\n')
replace_once(
    "docs/vspw-device-protocol.md",
    'VSPD v1.0 uses a fixed 40-byte header:\n',
    'VSPD v1.1 uses the same fixed 40-byte header:\n')
replace_once(
    "docs/vspw-device-protocol.md",
    '| 5 | 1 | version_minor | `0` |\n',
    '| 5 | 1 | version_minor | `1` |\n')
replace_once(
    "docs/vspw-device-protocol.md",
    '''| 15 | `LINK_STATE_EVENT` | asynchronous event |\n\nSynchronous requests use a non-zero `request_id`.''',
    '''| 15 | `LINK_STATE_EVENT` | asynchronous event |\n| 16 | `GET_SERVER_INFO` | non-owning management request/response |\n| 17 | `GET_PORT_INFO` | non-owning management request/response |\n| 18 | `GET_PORT_STATISTICS` | non-owning management request/response |\n| 19 | `CLEAR_PORT_STATISTICS` | non-owning management request/response |\n\nSynchronous requests use a non-zero `request_id`.''')
replace_once(
    "docs/vspw-device-protocol.md",
    '''byte 0  major = 1\nbyte 1  minor = 0\nbyte 2  reserved = 0\nbyte 3  reserved = 0\n```\n\nThe v1.0 codec currently requires an exact 1.0 match.''',
    '''byte 0  major = 1\nbyte 1  minor = 1\nbyte 2  reserved = 0\nbyte 3  reserved = 0\n```\n\nThe v1.1 codec currently requires an exact 1.1 match.''')
replace_once(
    "docs/vspw-device-protocol.md",
    '''A later management protocol used by `spwctl` may configure topology, but that remains distinct from ordinary application packet I/O.\n''',
    '''VSPD 1.1 adds a separate non-owning management connection used by `spwctl`. A management client performs HELLO but never ATTACHes to a virtual port. It can discover daemon bounds, inspect per-port ownership/link/queue state, read per-port statistics, and clear statistics without displacing the application owner. Lifecycle overrides and topology mutation are deliberately not part of v1.1.\n''')
replace_once(
    "docs/vspw-device-protocol.md",
    '''`CLEAR_STATISTICS` has no request or success-response payload.\n\n## Blocking, non-blocking and `poll()` direction\n''',
    '''`CLEAR_STATISTICS` has no request or success-response payload.\n\n## Management payloads\n\n`GET_SERVER_INFO` is valid after HELLO without ATTACH. Its 20-byte success payload is five network-order `u32` values: port count, client capacity, packet queue depth, time-code queue depth, and maximum logical packet size.\n\n`GET_PORT_INFO` is also HELLO-only and uses the header `port_id` as the queried port. Its 16-byte success payload contains flags, link state, queued packet count, and queued time-code count. Flags report attached, started, reset-latched and ever-attached state.\n\n`GET_PORT_STATISTICS` returns the normal 72-byte statistics payload for the selected port. `CLEAR_PORT_STATISTICS` clears those counters and returns no payload. These operations do not ATTACH, START, STOP, RESET, dequeue traffic, or alter application ownership.\n\n## Blocking, non-blocking and `poll()` direction\n''')

replace_once(
    "docs/vspwd.md",
    '''## Initial topology\n''',
    '''## Management with `spwctl`\n\nBuild the optional pure-C tools with `SPWKIT_BUILD_TOOLS=ON`. `spwctl` uses a HELLO-only VSPD management connection and never ATTACHes to an application port.\n\n```sh\nspwctl list\nspwctl show 0\nspwctl stats 0\nspwctl clear-stats 0\nspwctl --socket /tmp/my-mission-vspwd.sock list\n```\n\n`list`/`show` expose attachment, started/reset state, link state and bounded queue occupancy. Statistics inspection and clearing operate on daemon counters without consuming DATA/TIME_CODE events. This slice intentionally does not let `spwctl` START/STOP/RESET an attached application-owned port; ownership semantics remain unambiguous until an explicit administrative override model is designed.\n\n## Initial topology\n''')

print("spwctl patch applied")
