// SPDX-License-Identifier: Apache-2.0

#include "backends/device/vspw_device_protocol.h"

#include <string.h>

static uint16_t vspd_read_be16(const uint8_t* p) {
    return (uint16_t)(((uint16_t)p[0] << 8u) | (uint16_t)p[1]);
}

static uint32_t vspd_read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24u) |
           ((uint32_t)p[1] << 16u) |
           ((uint32_t)p[2] << 8u) |
           (uint32_t)p[3];
}

static uint64_t vspd_read_be64(const uint8_t* p) {
    return ((uint64_t)vspd_read_be32(p) << 32u) |
           (uint64_t)vspd_read_be32(p + 4u);
}

static void vspd_write_be16(uint8_t* p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8u);
    p[1] = (uint8_t)value;
}

static void vspd_write_be32(uint8_t* p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24u);
    p[1] = (uint8_t)(value >> 16u);
    p[2] = (uint8_t)(value >> 8u);
    p[3] = (uint8_t)value;
}

static void vspd_write_be64(uint8_t* p, uint64_t value) {
    vspd_write_be32(p, (uint32_t)(value >> 32u));
    vspd_write_be32(p + 4u, (uint32_t)value);
}

static int vspd_type_valid(uint8_t type) {
    return type >= VSPD_MSG_HELLO && type <= VSPD_MSG_LINK_STATE_EVENT;
}

static int vspd_type_is_event(uint8_t type) {
    return type == VSPD_MSG_DATA_RX ||
           type == VSPD_MSG_TIME_CODE_RX ||
           type == VSPD_MSG_LINK_STATE_EVENT;
}

static int vspd_type_allows_response(uint8_t type) {
    return !vspd_type_is_event(type);
}

static int vspd_has_data_flags(uint8_t flags) {
    return (flags & (VSPD_FLAG_FRAGMENT_START |
                     VSPD_FLAG_FRAGMENT_END |
                     VSPD_FLAG_EOP |
                     VSPD_FLAG_EEP)) != 0u;
}

static uint32_t vspd_success_response_payload_size(uint8_t type) {
    switch (type) {
        case VSPD_MSG_HELLO:
            return VSPD_HELLO_PAYLOAD_SIZE;
        case VSPD_MSG_GET_LINK_STATE:
            return VSPD_LINK_STATE_PAYLOAD_SIZE;
        case VSPD_MSG_GET_CAPABILITIES:
            return VSPD_CAPABILITIES_PAYLOAD_SIZE;
        case VSPD_MSG_GET_STATISTICS:
            return VSPD_STATISTICS_PAYLOAD_SIZE;
        default:
            return 0u;
    }
}

static vspd_codec_result_t vspd_validate_common(const vspd_header_t* header) {
    if (header == NULL) {
        return VSPD_CODEC_INVALID_ARGUMENT;
    }
    if (header->magic != VSPD_MAGIC) {
        return VSPD_CODEC_INVALID_MAGIC;
    }
    if (header->version_major != VSPD_VERSION_MAJOR ||
        header->version_minor != VSPD_VERSION_MINOR) {
        return VSPD_CODEC_UNSUPPORTED_VERSION;
    }
    if (header->header_size != VSPD_HEADER_SIZE || header->reserved != 0u) {
        return VSPD_CODEC_INVALID_HEADER;
    }
    if (!vspd_type_valid(header->type)) {
        return VSPD_CODEC_INVALID_TYPE;
    }
    if ((header->flags & (uint8_t)~VSPD_FLAG_KNOWN_MASK) != 0u) {
        return VSPD_CODEC_INVALID_FLAGS;
    }
    if (header->payload_size > VSPD_MAX_FRAME_PAYLOAD) {
        return VSPD_CODEC_INVALID_SIZE;
    }
    return VSPD_CODEC_OK;
}

static vspd_codec_result_t vspd_validate_data_shape(const vspd_header_t* header) {
    const uint8_t start = header->flags & VSPD_FLAG_FRAGMENT_START;
    const uint8_t end = header->flags & VSPD_FLAG_FRAGMENT_END;
    const uint8_t eop = header->flags & VSPD_FLAG_EOP;
    const uint8_t eep = header->flags & VSPD_FLAG_EEP;
    const uint64_t fragment_end =
        (uint64_t)header->fragment_offset + (uint64_t)header->payload_size;

    if (header->message_id == 0u || header->total_size > VSPD_MAX_LOGICAL_PACKET) {
        return VSPD_CODEC_INVALID_SHAPE;
    }
    if (fragment_end > (uint64_t)header->total_size) {
        return VSPD_CODEC_INVALID_SHAPE;
    }

    if (header->total_size == 0u) {
        if (header->payload_size != 0u || header->fragment_offset != 0u ||
            start == 0u || end == 0u) {
            return VSPD_CODEC_INVALID_SHAPE;
        }
    } else {
        if (header->payload_size == 0u) {
            return VSPD_CODEC_INVALID_SHAPE;
        }
        if ((header->fragment_offset == 0u) != (start != 0u)) {
            return VSPD_CODEC_INVALID_SHAPE;
        }
        if ((fragment_end == (uint64_t)header->total_size) != (end != 0u)) {
            return VSPD_CODEC_INVALID_SHAPE;
        }
    }

    if (end != 0u) {
        if ((eop != 0u) == (eep != 0u)) {
            return VSPD_CODEC_INVALID_SHAPE;
        }
    } else if (eop != 0u || eep != 0u) {
        return VSPD_CODEC_INVALID_SHAPE;
    }

    return VSPD_CODEC_OK;
}

static vspd_codec_result_t vspd_validate_payload_shape(const vspd_header_t* header,
                                                        const uint8_t* payload) {
    const int response = (header->flags & VSPD_FLAG_RESPONSE) != 0u;

    if (response) {
        uint32_t expected = 0u;
        if (!vspd_type_allows_response(header->type) || header->request_id == 0u ||
            vspd_has_data_flags(header->flags) || header->message_id != 0u ||
            header->fragment_offset != 0u || header->total_size != 0u) {
            return VSPD_CODEC_INVALID_SHAPE;
        }
        if (header->status == 0) {
            expected = vspd_success_response_payload_size(header->type);
        }
        if (header->payload_size != expected) {
            return VSPD_CODEC_INVALID_SHAPE;
        }
    } else {
        if (header->status != 0) {
            return VSPD_CODEC_INVALID_SHAPE;
        }
        if (vspd_type_is_event(header->type)) {
            if (header->request_id != 0u) {
                return VSPD_CODEC_INVALID_SHAPE;
            }
        } else if (header->request_id == 0u) {
            return VSPD_CODEC_INVALID_SHAPE;
        }

        if (header->type == VSPD_MSG_DATA_TX || header->type == VSPD_MSG_DATA_RX) {
            vspd_codec_result_t result = vspd_validate_data_shape(header);
            if (result != VSPD_CODEC_OK) {
                return result;
            }
        } else {
            if (vspd_has_data_flags(header->flags) || header->message_id != 0u ||
                header->fragment_offset != 0u || header->total_size != 0u) {
                return VSPD_CODEC_INVALID_SHAPE;
            }

            switch (header->type) {
                case VSPD_MSG_HELLO:
                    if (header->payload_size != VSPD_HELLO_PAYLOAD_SIZE) {
                        return VSPD_CODEC_INVALID_SHAPE;
                    }
                    break;
                case VSPD_MSG_ATTACH:
                case VSPD_MSG_DETACH:
                case VSPD_MSG_START:
                case VSPD_MSG_STOP:
                case VSPD_MSG_RESET:
                case VSPD_MSG_GET_LINK_STATE:
                case VSPD_MSG_GET_CAPABILITIES:
                case VSPD_MSG_GET_STATISTICS:
                case VSPD_MSG_CLEAR_STATISTICS:
                    if (header->payload_size != 0u) {
                        return VSPD_CODEC_INVALID_SHAPE;
                    }
                    break;
                case VSPD_MSG_TIME_CODE_TX:
                case VSPD_MSG_TIME_CODE_RX:
                    if (header->payload_size != VSPD_TIME_CODE_PAYLOAD_SIZE) {
                        return VSPD_CODEC_INVALID_SHAPE;
                    }
                    break;
                case VSPD_MSG_LINK_STATE_EVENT:
                    if (header->payload_size != VSPD_LINK_STATE_PAYLOAD_SIZE) {
                        return VSPD_CODEC_INVALID_SHAPE;
                    }
                    break;
                default:
                    break;
            }
        }
    }

    if (payload == NULL && header->payload_size != 0u) {
        return VSPD_CODEC_INVALID_ARGUMENT;
    }

    if (payload != NULL) {
        if (header->type == VSPD_MSG_HELLO && header->payload_size == 4u) {
            if (payload[0] != VSPD_VERSION_MAJOR ||
                payload[1] != VSPD_VERSION_MINOR ||
                payload[2] != 0u || payload[3] != 0u) {
                return VSPD_CODEC_UNSUPPORTED_VERSION;
            }
        }
        if ((header->type == VSPD_MSG_TIME_CODE_TX ||
             header->type == VSPD_MSG_TIME_CODE_RX) &&
            header->payload_size == VSPD_TIME_CODE_PAYLOAD_SIZE) {
            if (payload[0] > 63u || payload[1] > 3u) {
                return VSPD_CODEC_INVALID_SHAPE;
            }
        }
        if ((header->type == VSPD_MSG_GET_LINK_STATE && response && header->status == 0) ||
            header->type == VSPD_MSG_LINK_STATE_EVENT) {
            if (header->payload_size == VSPD_LINK_STATE_PAYLOAD_SIZE &&
                vspd_read_be32(payload) > VSPD_LINK_RUN) {
                return VSPD_CODEC_INVALID_SHAPE;
            }
        }
    }

    return VSPD_CODEC_OK;
}

vspd_codec_result_t vspd_encode_header(const vspd_header_t* header,
                                       uint8_t out[VSPD_HEADER_SIZE]) {
    vspd_codec_result_t result;
    if (out == NULL) {
        return VSPD_CODEC_INVALID_ARGUMENT;
    }
    result = vspd_validate_common(header);
    if (result != VSPD_CODEC_OK) {
        return result;
    }

    vspd_write_be32(out + 0u, header->magic);
    out[4] = header->version_major;
    out[5] = header->version_minor;
    out[6] = header->type;
    out[7] = header->flags;
    vspd_write_be16(out + 8u, header->header_size);
    vspd_write_be16(out + 10u, header->reserved);
    vspd_write_be32(out + 12u, header->payload_size);
    vspd_write_be32(out + 16u, header->request_id);
    vspd_write_be32(out + 20u, header->port_id);
    vspd_write_be32(out + 24u, header->message_id);
    vspd_write_be32(out + 28u, header->fragment_offset);
    vspd_write_be32(out + 32u, header->total_size);
    vspd_write_be32(out + 36u, (uint32_t)header->status);
    return VSPD_CODEC_OK;
}

vspd_codec_result_t vspd_decode_header(const uint8_t* data,
                                       size_t size,
                                       vspd_header_t* out_header) {
    vspd_codec_result_t result;
    if (data == NULL || out_header == NULL) {
        return VSPD_CODEC_INVALID_ARGUMENT;
    }
    if (size < VSPD_HEADER_SIZE) {
        return VSPD_CODEC_TRUNCATED;
    }

    out_header->magic = vspd_read_be32(data + 0u);
    out_header->version_major = data[4];
    out_header->version_minor = data[5];
    out_header->type = data[6];
    out_header->flags = data[7];
    out_header->header_size = vspd_read_be16(data + 8u);
    out_header->reserved = vspd_read_be16(data + 10u);
    out_header->payload_size = vspd_read_be32(data + 12u);
    out_header->request_id = vspd_read_be32(data + 16u);
    out_header->port_id = vspd_read_be32(data + 20u);
    out_header->message_id = vspd_read_be32(data + 24u);
    out_header->fragment_offset = vspd_read_be32(data + 28u);
    out_header->total_size = vspd_read_be32(data + 32u);
    out_header->status = (int32_t)vspd_read_be32(data + 36u);

    result = vspd_validate_common(out_header);
    if (result != VSPD_CODEC_OK) {
        memset(out_header, 0, sizeof(*out_header));
    }
    return result;
}

vspd_codec_result_t vspd_validate_frame(const uint8_t* frame,
                                        size_t frame_size,
                                        vspd_header_t* out_header) {
    vspd_header_t header;
    vspd_codec_result_t result;
    const uint8_t* payload;

    if (frame == NULL) {
        return VSPD_CODEC_INVALID_ARGUMENT;
    }
    result = vspd_decode_header(frame, frame_size, &header);
    if (result != VSPD_CODEC_OK) {
        return result;
    }
    if (frame_size != (size_t)VSPD_HEADER_SIZE + (size_t)header.payload_size) {
        return frame_size < (size_t)VSPD_HEADER_SIZE + (size_t)header.payload_size
                   ? VSPD_CODEC_TRUNCATED
                   : VSPD_CODEC_INVALID_SIZE;
    }

    payload = header.payload_size == 0u ? NULL : frame + VSPD_HEADER_SIZE;
    result = vspd_validate_payload_shape(&header, payload);
    if (result != VSPD_CODEC_OK) {
        return result;
    }
    if (out_header != NULL) {
        *out_header = header;
    }
    return VSPD_CODEC_OK;
}

void vspd_encode_u32_payload(uint32_t value, uint8_t out[4]) {
    if (out != NULL) {
        vspd_write_be32(out, value);
    }
}

uint32_t vspd_decode_u32_payload(const uint8_t in[4]) {
    return in == NULL ? 0u : vspd_read_be32(in);
}

void vspd_encode_capabilities(const vspd_capabilities_payload_t* value,
                              uint8_t out[VSPD_CAPABILITIES_PAYLOAD_SIZE]) {
    if (value == NULL || out == NULL) {
        return;
    }
    vspd_write_be64(out + 0u, value->bits);
    vspd_write_be32(out + 8u, value->max_packet_size);
    vspd_write_be32(out + 12u, value->tx_queue_depth);
    vspd_write_be32(out + 16u, value->rx_queue_depth);
    vspd_write_be32(out + 20u, value->buffer_alignment);
}

void vspd_decode_capabilities(const uint8_t in[VSPD_CAPABILITIES_PAYLOAD_SIZE],
                              vspd_capabilities_payload_t* out) {
    if (in == NULL || out == NULL) {
        return;
    }
    out->bits = vspd_read_be64(in + 0u);
    out->max_packet_size = vspd_read_be32(in + 8u);
    out->tx_queue_depth = vspd_read_be32(in + 12u);
    out->rx_queue_depth = vspd_read_be32(in + 16u);
    out->buffer_alignment = vspd_read_be32(in + 20u);
}

void vspd_encode_statistics(const vspd_statistics_payload_t* value,
                            uint8_t out[VSPD_STATISTICS_PAYLOAD_SIZE]) {
    const uint64_t* values;
    size_t i;
    if (value == NULL || out == NULL) {
        return;
    }
    values = &value->tx_packets;
    for (i = 0u; i < 9u; ++i) {
        vspd_write_be64(out + i * 8u, values[i]);
    }
}

void vspd_decode_statistics(const uint8_t in[VSPD_STATISTICS_PAYLOAD_SIZE],
                            vspd_statistics_payload_t* out) {
    uint64_t* values;
    size_t i;
    if (in == NULL || out == NULL) {
        return;
    }
    values = &out->tx_packets;
    for (i = 0u; i < 9u; ++i) {
        values[i] = vspd_read_be64(in + i * 8u);
    }
}
