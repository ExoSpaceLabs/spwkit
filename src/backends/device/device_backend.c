// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L

#include "backends/device/device_backend.h"
#include "backends/device/vspw_device_protocol.h"

#include <spwkit/device.h>

#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

#define DEVICE_TIME_CODE_QUEUE_DEPTH 8u

typedef struct device_context {
    spw_device_config_t config;
    int fd;
    uint32_t next_request_id;
    uint32_t next_message_id;
    bool desired_started;
    spw_link_state_t state;
    spw_capabilities_t capabilities;

    bool rx_active;
    bool rx_ready;
    uint32_t rx_message_id;
    uint32_t rx_total_size;
    uint32_t rx_next_offset;
    spw_terminator_t rx_terminator;
    uint8_t rx_data[VSPD_MAX_LOGICAL_PACKET];

    spw_time_code_t time_codes[DEVICE_TIME_CODE_QUEUE_DEPTH];
    size_t time_code_head;
    size_t time_code_count;
} device_context_t;

static uint64_t monotonic_us(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0u;
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000);
}

static uint64_t deadline_for(spw_timeout_us_t timeout_us) {
    uint64_t now;
    if (timeout_us == SPW_TIMEOUT_INFINITE) {
        return UINT64_MAX;
    }
    now = monotonic_us();
    if (UINT64_MAX - now < timeout_us) {
        return UINT64_MAX - 1u;
    }
    return now + timeout_us;
}

static int poll_timeout_ms(uint64_t deadline) {
    uint64_t now;
    uint64_t remaining;
    if (deadline == UINT64_MAX) {
        return -1;
    }
    now = monotonic_us();
    if (now >= deadline) {
        return 0;
    }
    remaining = deadline - now;
    if (remaining > (uint64_t)INT32_MAX * 1000u) {
        return INT32_MAX;
    }
    return (int)((remaining + 999u) / 1000u);
}

static spw_result_t status_to_result(int32_t status) {
    switch (status) {
        case VSPD_STATUS_OK: return SPW_OK;
        case VSPD_STATUS_INVALID_ARGUMENT: return SPW_ERR_INVALID_ARGUMENT;
        case VSPD_STATUS_INVALID_STATE: return SPW_ERR_INVALID_STATE;
        case VSPD_STATUS_TIMEOUT: return SPW_ERR_TIMEOUT;
        case VSPD_STATUS_UNSUPPORTED: return SPW_ERR_UNSUPPORTED;
        case VSPD_STATUS_RESOURCE_EXHAUSTED: return SPW_ERR_RESOURCE_EXHAUSTED;
        case VSPD_STATUS_LINK_UNAVAILABLE: return SPW_ERR_LINK_UNAVAILABLE;
        case VSPD_STATUS_BUFFER_TOO_SMALL: return SPW_ERR_BUFFER_TOO_SMALL;
        case VSPD_STATUS_INVALID_PACKET: return SPW_ERR_INVALID_PACKET;
        default: return SPW_ERR_BACKEND;
    }
}

static void clear_rx(device_context_t* context) {
    context->rx_active = false;
    context->rx_ready = false;
    context->rx_message_id = 0u;
    context->rx_total_size = 0u;
    context->rx_next_offset = 0u;
    context->rx_terminator = SPW_TERMINATOR_EOP;
}

static void mark_disconnected(device_context_t* context) {
    if (context->fd >= 0) {
        close(context->fd);
    }
    context->fd = -1;
    context->state = SPW_LINK_ERROR_WAIT;
    clear_rx(context);
    context->time_code_head = 0u;
    context->time_code_count = 0u;
}

static spw_result_t wait_fd(device_context_t* context,
                            short events,
                            uint64_t deadline) {
    struct pollfd descriptor;
    int result;
    for (;;) {
        descriptor.fd = context->fd;
        descriptor.events = events;
        descriptor.revents = 0;
        result = poll(&descriptor, 1u, poll_timeout_ms(deadline));
        if (result > 0) {
            if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                mark_disconnected(context);
                return SPW_ERR_LINK_UNAVAILABLE;
            }
            if ((descriptor.revents & events) != 0) {
                return SPW_OK;
            }
            continue;
        }
        if (result == 0) {
            return SPW_ERR_TIMEOUT;
        }
        if (errno != EINTR) {
            mark_disconnected(context);
            return SPW_ERR_LINK_UNAVAILABLE;
        }
    }
}

static spw_result_t send_record(device_context_t* context,
                                const uint8_t* data,
                                size_t size,
                                uint64_t deadline) {
    for (;;) {
        ssize_t sent;
        spw_result_t result = wait_fd(context, POLLOUT, deadline);
        if (result != SPW_OK) {
            return result;
        }
        sent = send(context->fd, data, size, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (sent == (ssize_t)size) {
            return SPW_OK;
        }
        if (sent >= 0) {
            mark_disconnected(context);
            return SPW_ERR_BACKEND;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            continue;
        }
        mark_disconnected(context);
        return SPW_ERR_LINK_UNAVAILABLE;
    }
}

static spw_result_t receive_record(device_context_t* context,
                                   uint8_t* frame,
                                   size_t capacity,
                                   size_t* out_size,
                                   uint64_t deadline) {
    for (;;) {
        ssize_t received;
        spw_result_t result = wait_fd(context, POLLIN, deadline);
        if (result != SPW_OK) {
            return result;
        }
        received = recv(context->fd, frame, capacity, MSG_DONTWAIT);
        if (received > 0) {
            *out_size = (size_t)received;
            return SPW_OK;
        }
        if (received == 0) {
            mark_disconnected(context);
            return SPW_ERR_LINK_UNAVAILABLE;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            continue;
        }
        mark_disconnected(context);
        return SPW_ERR_LINK_UNAVAILABLE;
    }
}

static bool queue_time_code(device_context_t* context,
                            const uint8_t* payload) {
    size_t index;
    if (context->time_code_count >= DEVICE_TIME_CODE_QUEUE_DEPTH) {
        return false;
    }
    index = (context->time_code_head + context->time_code_count) %
            DEVICE_TIME_CODE_QUEUE_DEPTH;
    context->time_codes[index].time_count = payload[0];
    context->time_codes[index].control_flags = payload[1];
    ++context->time_code_count;
    return true;
}

static spw_result_t process_event(device_context_t* context,
                                  const vspd_header_t* header,
                                  const uint8_t* payload) {
    if (header->type == VSPD_MSG_LINK_STATE_EVENT) {
        context->state = (spw_link_state_t)vspd_decode_u32_payload(payload);
        return SPW_OK;
    }
    if (header->type == VSPD_MSG_TIME_CODE_RX) {
        return queue_time_code(context, payload) ? SPW_OK
                                                 : SPW_ERR_RESOURCE_EXHAUSTED;
    }
    if (header->type == VSPD_MSG_DATA_RX) {
        bool start = (header->flags & VSPD_FLAG_FRAGMENT_START) != 0u;
        bool end = (header->flags & VSPD_FLAG_FRAGMENT_END) != 0u;
        if (context->rx_ready) {
            return SPW_ERR_RESOURCE_EXHAUSTED;
        }
        if (start) {
            if (context->rx_active || header->fragment_offset != 0u ||
                header->total_size > VSPD_MAX_LOGICAL_PACKET) {
                return SPW_ERR_BACKEND;
            }
            context->rx_active = true;
            context->rx_message_id = header->message_id;
            context->rx_total_size = header->total_size;
            context->rx_next_offset = 0u;
        } else if (!context->rx_active ||
                   context->rx_message_id != header->message_id ||
                   context->rx_total_size != header->total_size) {
            return SPW_ERR_BACKEND;
        }
        if (header->fragment_offset != context->rx_next_offset ||
            header->payload_size > context->rx_total_size - context->rx_next_offset) {
            clear_rx(context);
            return SPW_ERR_BACKEND;
        }
        if (header->payload_size != 0u) {
            memcpy(context->rx_data + context->rx_next_offset,
                   payload,
                   header->payload_size);
        }
        context->rx_next_offset += header->payload_size;
        if (end) {
            if (context->rx_next_offset != context->rx_total_size) {
                clear_rx(context);
                return SPW_ERR_BACKEND;
            }
            context->rx_terminator =
                (header->flags & VSPD_FLAG_EEP) != 0u ? SPW_TERMINATOR_EEP
                                                      : SPW_TERMINATOR_EOP;
            context->rx_active = false;
            context->rx_ready = true;
        }
        return SPW_OK;
    }
    return SPW_ERR_BACKEND;
}

static spw_result_t wait_response(device_context_t* context,
                                  uint8_t type,
                                  uint32_t request_id,
                                  int32_t* out_status,
                                  uint8_t* response_payload,
                                  uint32_t response_capacity,
                                  uint32_t* out_response_size,
                                  uint64_t deadline) {
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];
    for (;;) {
        vspd_header_t header;
        const uint8_t* payload;
        size_t frame_size = 0u;
        spw_result_t result = receive_record(context,
                                             frame,
                                             sizeof(frame),
                                             &frame_size,
                                             deadline);
        if (result != SPW_OK) {
            return result;
        }
        if (vspd_validate_frame(frame, frame_size, &header) != VSPD_CODEC_OK) {
            mark_disconnected(context);
            return SPW_ERR_BACKEND;
        }
        payload = header.payload_size == 0u ? NULL : frame + VSPD_HEADER_SIZE;
        if ((header.flags & VSPD_FLAG_RESPONSE) != 0u) {
            if (header.type != type || header.request_id != request_id) {
                mark_disconnected(context);
                return SPW_ERR_BACKEND;
            }
            if (header.payload_size > response_capacity) {
                return SPW_ERR_BACKEND;
            }
            if (response_payload != NULL && header.payload_size != 0u) {
                memcpy(response_payload, payload, header.payload_size);
            }
            if (out_response_size != NULL) {
                *out_response_size = header.payload_size;
            }
            if (out_status != NULL) {
                *out_status = header.status;
            }
            return SPW_OK;
        }
        result = process_event(context, &header, payload);
        if (result != SPW_OK) {
            return result;
        }
    }
}

static spw_result_t send_request(device_context_t* context,
                                 uint8_t type,
                                 const uint8_t* payload,
                                 uint32_t payload_size,
                                 uint8_t* response_payload,
                                 uint32_t response_capacity,
                                 uint32_t* out_response_size,
                                 uint64_t deadline) {
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_STATISTICS_PAYLOAD_SIZE];
    vspd_header_t header;
    uint32_t request_id = context->next_request_id++;
    int32_t status = VSPD_STATUS_BACKEND;
    spw_result_t result;

    if (payload_size > VSPD_STATISTICS_PAYLOAD_SIZE) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    memset(&header, 0, sizeof(header));
    header.magic = VSPD_MAGIC;
    header.version_major = VSPD_VERSION_MAJOR;
    header.version_minor = VSPD_VERSION_MINOR;
    header.type = type;
    header.header_size = VSPD_HEADER_SIZE;
    header.payload_size = payload_size;
    header.request_id = request_id;
    header.port_id = context->config.port_id;
    if (vspd_encode_header(&header, frame) != VSPD_CODEC_OK) {
        return SPW_ERR_BACKEND;
    }
    if (payload_size != 0u) {
        memcpy(frame + VSPD_HEADER_SIZE, payload, payload_size);
    }
    result = send_record(context, frame, VSPD_HEADER_SIZE + payload_size, deadline);
    if (result != SPW_OK) {
        return result;
    }
    result = wait_response(context,
                           type,
                           request_id,
                           &status,
                           response_payload,
                           response_capacity,
                           out_response_size,
                           deadline);
    return result == SPW_OK ? status_to_result(status) : result;
}

static spw_result_t connect_session(device_context_t* context,
                                    uint64_t deadline) {
    struct sockaddr_un address;
    uint8_t hello[VSPD_HELLO_PAYLOAD_SIZE] = {
        VSPD_VERSION_MAJOR, VSPD_VERSION_MINOR, 0u, 0u};
    uint8_t response[VSPD_CAPABILITIES_PAYLOAD_SIZE];
    uint32_t response_size = 0u;
    spw_result_t result;

    context->fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (context->fd < 0) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path,
           context->config.endpoint,
           strlen(context->config.endpoint) + 1u);
    if (connect(context->fd,
                (const struct sockaddr*)&address,
                sizeof(address)) != 0) {
        mark_disconnected(context);
        return SPW_ERR_LINK_UNAVAILABLE;
    }

    result = send_request(context,
                          VSPD_MSG_HELLO,
                          hello,
                          VSPD_HELLO_PAYLOAD_SIZE,
                          response,
                          sizeof(response),
                          &response_size,
                          deadline);
    if (result != SPW_OK || response_size != VSPD_HELLO_PAYLOAD_SIZE ||
        memcmp(response, hello, VSPD_HELLO_PAYLOAD_SIZE) != 0) {
        mark_disconnected(context);
        return result == SPW_OK ? SPW_ERR_UNSUPPORTED : result;
    }
    result = send_request(context,
                          VSPD_MSG_ATTACH,
                          NULL,
                          0u,
                          NULL,
                          0u,
                          NULL,
                          deadline);
    if (result != SPW_OK) {
        mark_disconnected(context);
        return result;
    }
    context->state = SPW_LINK_ERROR_RESET;
    clear_rx(context);
    context->time_code_head = 0u;
    context->time_code_count = 0u;

    result = send_request(context,
                          VSPD_MSG_GET_CAPABILITIES,
                          NULL,
                          0u,
                          response,
                          sizeof(response),
                          &response_size,
                          deadline);
    if (result != SPW_OK || response_size != VSPD_CAPABILITIES_PAYLOAD_SIZE) {
        mark_disconnected(context);
        return result == SPW_OK ? SPW_ERR_BACKEND : result;
    }
    {
        vspd_capabilities_payload_t wire;
        vspd_decode_capabilities(response, &wire);
        context->capabilities.bits = wire.bits;
        context->capabilities.max_packet_size = wire.max_packet_size;
        context->capabilities.tx_queue_depth = wire.tx_queue_depth;
        context->capabilities.rx_queue_depth = wire.rx_queue_depth;
        context->capabilities.buffer_alignment = wire.buffer_alignment;
    }
    if (context->desired_started) {
        result = send_request(context,
                              VSPD_MSG_START,
                              NULL,
                              0u,
                              NULL,
                              0u,
                              NULL,
                              deadline);
        if (result != SPW_OK) {
            mark_disconnected(context);
            return result;
        }
    }
    return SPW_OK;
}

static spw_result_t ensure_connected(device_context_t* context,
                                     uint64_t deadline) {
    if (context->fd >= 0) {
        return SPW_OK;
    }
    return connect_session(context, deadline);
}

static spw_result_t request_simple(device_context_t* context,
                                   uint8_t type,
                                   spw_timeout_us_t timeout_us) {
    uint64_t deadline = deadline_for(timeout_us);
    spw_result_t result = ensure_connected(context, deadline);
    if (result != SPW_OK) {
        return result;
    }
    return send_request(context,
                        type,
                        NULL,
                        0u,
                        NULL,
                        0u,
                        NULL,
                        deadline);
}

static spw_result_t device_construct(void* raw,
                                     const spw_port_config_t* config) {
    device_context_t* context = (device_context_t*)raw;
    const spw_device_config_t* device =
        (const spw_device_config_t*)config->backend_config;
    memset(context, 0, sizeof(*context));
    context->config = *device;
    context->fd = -1;
    context->next_request_id = 1u;
    context->next_message_id = 1u;
    context->state = SPW_LINK_ERROR_RESET;
    return connect_session(context, deadline_for(UINT64_C(1000000)));
}

static void device_destroy(void* raw) {
    device_context_t* context = (device_context_t*)raw;
    if (context->fd >= 0) {
        (void)send_request(context,
                           VSPD_MSG_DETACH,
                           NULL,
                           0u,
                           NULL,
                           0u,
                           NULL,
                           deadline_for(UINT64_C(100000)));
        if (context->fd >= 0) {
            close(context->fd);
        }
    }
    context->fd = -1;
}

static spw_result_t device_start(void* raw) {
    device_context_t* context = (device_context_t*)raw;
    spw_result_t result;
    context->desired_started = true;
    result = request_simple(context, VSPD_MSG_START, SPW_TIMEOUT_INFINITE);
    if (result != SPW_OK) {
        return result;
    }
    return SPW_OK;
}

static spw_result_t device_stop(void* raw) {
    device_context_t* context = (device_context_t*)raw;
    spw_result_t result;
    context->desired_started = false;
    result = request_simple(context, VSPD_MSG_STOP, SPW_TIMEOUT_INFINITE);
    return result;
}

static spw_result_t device_reset(void* raw) {
    device_context_t* context = (device_context_t*)raw;
    spw_result_t result;
    context->desired_started = false;
    result = request_simple(context, VSPD_MSG_RESET, SPW_TIMEOUT_INFINITE);
    if (result == SPW_OK) {
        clear_rx(context);
        context->time_code_head = 0u;
        context->time_code_count = 0u;
    }
    return result;
}

static spw_result_t device_get_link_state(const void* raw,
                                          spw_link_state_t* out_state) {
    device_context_t* context = (device_context_t*)raw;
    uint8_t payload[VSPD_LINK_STATE_PAYLOAD_SIZE];
    uint32_t size = 0u;
    uint64_t deadline = deadline_for(UINT64_C(250000));
    spw_result_t result = ensure_connected(context, deadline);
    if (result != SPW_OK) {
        *out_state = context->state;
        return result;
    }
    result = send_request(context,
                          VSPD_MSG_GET_LINK_STATE,
                          NULL,
                          0u,
                          payload,
                          sizeof(payload),
                          &size,
                          deadline);
    if (result == SPW_OK && size == VSPD_LINK_STATE_PAYLOAD_SIZE) {
        context->state = (spw_link_state_t)vspd_decode_u32_payload(payload);
    }
    *out_state = context->state;
    return result;
}

static spw_result_t device_get_capabilities(const void* raw,
                                            spw_capabilities_t* out_capabilities) {
    const device_context_t* context = (const device_context_t*)raw;
    *out_capabilities = context->capabilities;
    return SPW_OK;
}

static bool device_supports_zero_copy(const void* raw) {
    (void)raw;
    return false;
}

static spw_result_t device_send(void* raw,
                                const spw_packet_t* packet,
                                spw_timeout_us_t timeout_us) {
    device_context_t* context = (device_context_t*)raw;
    uint64_t deadline = deadline_for(timeout_us);
    uint32_t request_id;
    uint32_t message_id;
    uint32_t offset = 0u;
    spw_result_t result = ensure_connected(context, deadline);
    if (result != SPW_OK) {
        return result;
    }
    request_id = context->next_request_id++;
    message_id = context->next_message_id++;

    do {
        uint8_t frame[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];
        vspd_header_t header;
        uint32_t remaining = (uint32_t)packet->length - offset;
        uint32_t chunk = remaining > VSPD_MAX_FRAME_PAYLOAD
                             ? VSPD_MAX_FRAME_PAYLOAD
                             : remaining;
        bool final_fragment = packet->length == 0u ||
                              offset + chunk == (uint32_t)packet->length;
        memset(&header, 0, sizeof(header));
        header.magic = VSPD_MAGIC;
        header.version_major = VSPD_VERSION_MAJOR;
        header.version_minor = VSPD_VERSION_MINOR;
        header.type = VSPD_MSG_DATA_TX;
        header.header_size = VSPD_HEADER_SIZE;
        header.payload_size = chunk;
        header.request_id = request_id;
        header.port_id = context->config.port_id;
        header.message_id = message_id;
        header.fragment_offset = offset;
        header.total_size = (uint32_t)packet->length;
        if (offset == 0u) {
            header.flags |= VSPD_FLAG_FRAGMENT_START;
        }
        if (final_fragment) {
            header.flags |= VSPD_FLAG_FRAGMENT_END;
            header.flags |= packet->terminator == SPW_TERMINATOR_EEP
                                ? VSPD_FLAG_EEP
                                : VSPD_FLAG_EOP;
        }
        if (vspd_encode_header(&header, frame) != VSPD_CODEC_OK) {
            return SPW_ERR_BACKEND;
        }
        if (chunk != 0u) {
            memcpy(frame + VSPD_HEADER_SIZE, packet->data + offset, chunk);
        }
        result = send_record(context, frame, VSPD_HEADER_SIZE + chunk, deadline);
        if (result != SPW_OK) {
            return result;
        }
        offset += chunk;
        if (final_fragment) {
            int32_t status = VSPD_STATUS_BACKEND;
            result = wait_response(context,
                                   VSPD_MSG_DATA_TX,
                                   request_id,
                                   &status,
                                   NULL,
                                   0u,
                                   NULL,
                                   deadline);
            return result == SPW_OK ? status_to_result(status) : result;
        }
    } while (offset < packet->length);
    return SPW_ERR_BACKEND;
}

static spw_result_t service_until_packet(device_context_t* context,
                                         uint64_t deadline) {
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];
    while (!context->rx_ready) {
        vspd_header_t header;
        const uint8_t* payload;
        size_t frame_size = 0u;
        spw_result_t result = receive_record(context,
                                             frame,
                                             sizeof(frame),
                                             &frame_size,
                                             deadline);
        if (result != SPW_OK) {
            return result;
        }
        if (vspd_validate_frame(frame, frame_size, &header) != VSPD_CODEC_OK ||
            (header.flags & VSPD_FLAG_RESPONSE) != 0u) {
            return SPW_ERR_BACKEND;
        }
        payload = header.payload_size == 0u ? NULL : frame + VSPD_HEADER_SIZE;
        result = process_event(context, &header, payload);
        if (result != SPW_OK) {
            return result;
        }
    }
    return SPW_OK;
}

static spw_result_t device_receive(void* raw,
                                   spw_packet_t* packet,
                                   spw_timeout_us_t timeout_us) {
    device_context_t* context = (device_context_t*)raw;
    uint64_t deadline = deadline_for(timeout_us);
    spw_result_t result = ensure_connected(context, deadline);
    if (result != SPW_OK) {
        return result;
    }
    if (!context->rx_ready) {
        result = service_until_packet(context, deadline);
        if (result != SPW_OK) {
            return result;
        }
    }
    packet->length = context->rx_total_size;
    packet->terminator = context->rx_terminator;
    if (packet->capacity < context->rx_total_size) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }
    if (context->rx_total_size != 0u) {
        memcpy(packet->data, context->rx_data, context->rx_total_size);
    }
    clear_rx(context);
    return SPW_OK;
}

static spw_result_t device_send_time_code(void* raw,
                                          const spw_time_code_t* time_code,
                                          spw_timeout_us_t timeout_us) {
    device_context_t* context = (device_context_t*)raw;
    uint8_t payload[VSPD_TIME_CODE_PAYLOAD_SIZE] = {
        time_code->time_count, time_code->control_flags};
    uint64_t deadline = deadline_for(timeout_us);
    spw_result_t result = ensure_connected(context, deadline);
    if (result != SPW_OK) {
        return result;
    }
    return send_request(context,
                        VSPD_MSG_TIME_CODE_TX,
                        payload,
                        sizeof(payload),
                        NULL,
                        0u,
                        NULL,
                        deadline);
}

static spw_result_t device_receive_time_code(void* raw,
                                             spw_time_code_t* time_code,
                                             spw_timeout_us_t timeout_us) {
    device_context_t* context = (device_context_t*)raw;
    uint64_t deadline = deadline_for(timeout_us);
    spw_result_t result = ensure_connected(context, deadline);
    if (result != SPW_OK) {
        return result;
    }
    while (context->time_code_count == 0u) {
        uint8_t frame[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];
        vspd_header_t header;
        const uint8_t* payload;
        size_t frame_size = 0u;
        result = receive_record(context, frame, sizeof(frame), &frame_size, deadline);
        if (result != SPW_OK) {
            return result;
        }
        if (vspd_validate_frame(frame, frame_size, &header) != VSPD_CODEC_OK ||
            (header.flags & VSPD_FLAG_RESPONSE) != 0u) {
            return SPW_ERR_BACKEND;
        }
        payload = header.payload_size == 0u ? NULL : frame + VSPD_HEADER_SIZE;
        result = process_event(context, &header, payload);
        if (result != SPW_OK) {
            return result;
        }
    }
    *time_code = context->time_codes[context->time_code_head];
    context->time_code_head =
        (context->time_code_head + 1u) % DEVICE_TIME_CODE_QUEUE_DEPTH;
    --context->time_code_count;
    return SPW_OK;
}

static spw_ready_events_t device_ready_events(
    const device_context_t* context,
    spw_ready_events_t interests) {
    spw_ready_events_t ready = SPW_READY_NONE;
    if ((interests & SPW_READY_RX_PACKET) != 0u && context->rx_ready) {
        ready |= SPW_READY_RX_PACKET;
    }
    if ((interests & SPW_READY_RX_TIME_CODE) != 0u &&
        context->time_code_count != 0u) {
        ready |= SPW_READY_RX_TIME_CODE;
    }
    return ready;
}

static spw_result_t device_service_event(device_context_t* context,
                                         uint64_t deadline) {
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];
    vspd_header_t header;
    const uint8_t* payload;
    size_t frame_size = 0u;
    spw_result_t result = receive_record(context,
                                         frame,
                                         sizeof(frame),
                                         &frame_size,
                                         deadline);
    if (result != SPW_OK) {
        return result;
    }
    if (vspd_validate_frame(frame, frame_size, &header) != VSPD_CODEC_OK ||
        (header.flags & VSPD_FLAG_RESPONSE) != 0u) {
        mark_disconnected(context);
        return SPW_ERR_BACKEND;
    }
    payload = header.payload_size == 0u ? NULL : frame + VSPD_HEADER_SIZE;
    return process_event(context, &header, payload);
}

static spw_result_t device_wait(void* raw,
                                spw_ready_events_t interests,
                                spw_timeout_us_t timeout_us,
                                spw_ready_events_t* out_ready) {
    device_context_t* context = (device_context_t*)raw;
    uint64_t deadline = deadline_for(timeout_us);
    spw_ready_events_t ready = device_ready_events(context, interests);
    spw_result_t result;

    *out_ready = SPW_READY_NONE;
    if (ready != SPW_READY_NONE) {
        *out_ready = ready;
        return SPW_OK;
    }

    result = ensure_connected(context, deadline);
    if (result != SPW_OK) {
        return result;
    }

    for (;;) {
        result = device_service_event(context, deadline);
        if (result != SPW_OK) {
            return result;
        }
        ready = device_ready_events(context, interests);
        if (ready != SPW_READY_NONE) {
            *out_ready = ready;
            return SPW_OK;
        }
    }
}

static spw_result_t device_get_statistics(const void* raw,
                                          spw_statistics_t* out_statistics) {
    device_context_t* context = (device_context_t*)raw;
    uint8_t payload[VSPD_STATISTICS_PAYLOAD_SIZE];
    uint32_t size = 0u;
    vspd_statistics_payload_t wire;
    uint64_t deadline = deadline_for(UINT64_C(250000));
    spw_result_t result = ensure_connected(context, deadline);
    if (result != SPW_OK) {
        return result;
    }
    result = send_request(context,
                          VSPD_MSG_GET_STATISTICS,
                          NULL,
                          0u,
                          payload,
                          sizeof(payload),
                          &size,
                          deadline);
    if (result != SPW_OK || size != VSPD_STATISTICS_PAYLOAD_SIZE) {
        return result == SPW_OK ? SPW_ERR_BACKEND : result;
    }
    vspd_decode_statistics(payload, &wire);
    out_statistics->tx_packets = wire.tx_packets;
    out_statistics->rx_packets = wire.rx_packets;
    out_statistics->tx_bytes = wire.tx_bytes;
    out_statistics->rx_bytes = wire.rx_bytes;
    out_statistics->tx_time_codes = wire.tx_time_codes;
    out_statistics->rx_time_codes = wire.rx_time_codes;
    out_statistics->eep_packets = wire.eep_packets;
    out_statistics->link_errors = wire.link_errors;
    out_statistics->dropped_packets = wire.dropped_packets;
    return SPW_OK;
}

static spw_result_t device_clear_statistics(void* raw) {
    return request_simple((device_context_t*)raw,
                          VSPD_MSG_CLEAR_STATISTICS,
                          UINT64_C(250000));
}

static spw_result_t device_get_fault_statistics(
    const void* raw,
    spw_fault_statistics_t* out_statistics) {
    (void)raw;
    memset(out_statistics, 0, sizeof(*out_statistics));
    return SPW_ERR_UNSUPPORTED;
}

static spw_result_t device_clear_fault_statistics(void* raw) {
    (void)raw;
    return SPW_ERR_UNSUPPORTED;
}

static spw_result_t unsupported_acquire_tx(void* raw,
                                           size_t capacity,
                                           spw_timeout_us_t timeout,
                                           spw_buffer_t** out) {
    (void)raw; (void)capacity; (void)timeout; (void)out;
    return SPW_ERR_UNSUPPORTED;
}

static spw_result_t unsupported_submit_tx(void* raw,
                                          spw_buffer_t* buffer,
                                          spw_timeout_us_t timeout) {
    (void)raw; (void)buffer; (void)timeout;
    return SPW_ERR_UNSUPPORTED;
}

static spw_result_t unsupported_reclaim_tx(void* raw,
                                           spw_timeout_us_t timeout,
                                           spw_buffer_t** out) {
    (void)raw; (void)timeout; (void)out;
    return SPW_ERR_UNSUPPORTED;
}

static spw_result_t unsupported_release_tx(void* raw, spw_buffer_t* buffer) {
    (void)raw; (void)buffer;
    return SPW_ERR_UNSUPPORTED;
}

static spw_result_t unsupported_acquire_rx(void* raw,
                                           spw_timeout_us_t timeout,
                                           spw_buffer_t** out) {
    (void)raw; (void)timeout; (void)out;
    return SPW_ERR_UNSUPPORTED;
}

static spw_result_t unsupported_release_rx(void* raw, spw_buffer_t* buffer) {
    (void)raw; (void)buffer;
    return SPW_ERR_UNSUPPORTED;
}

static const spw_backend_ops_t DEVICE_OPS = {
    device_start,
    device_stop,
    device_reset,
    device_get_link_state,
    device_get_capabilities,
    device_supports_zero_copy,
    device_send,
    device_receive,
    device_send_time_code,
    device_receive_time_code,
    device_get_statistics,
    device_clear_statistics,
    device_get_fault_statistics,
    device_clear_fault_statistics,
    unsupported_acquire_tx,
    unsupported_submit_tx,
    unsupported_reclaim_tx,
    unsupported_release_tx,
    unsupported_acquire_rx,
    unsupported_release_rx,
    device_wait};

static const spw_backend_factory_t DEVICE_FACTORY = {
    sizeof(device_context_t),
    alignof(device_context_t),
    device_construct,
    device_destroy,
    &DEVICE_OPS};

const spw_backend_factory_t* spw_device_backend_factory(void) {
    return &DEVICE_FACTORY;
}
