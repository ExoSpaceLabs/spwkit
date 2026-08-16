// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/vspw_tp.h"

static void write_u16(uint8_t* dst, uint16_t value) {
    dst[0] = (uint8_t)((value >> 8u) & 0xffu);
    dst[1] = (uint8_t)(value & 0xffu);
}

static void write_u32(uint8_t* dst, uint32_t value) {
    dst[0] = (uint8_t)((value >> 24u) & 0xffu);
    dst[1] = (uint8_t)((value >> 16u) & 0xffu);
    dst[2] = (uint8_t)((value >> 8u) & 0xffu);
    dst[3] = (uint8_t)(value & 0xffu);
}

static void write_u64(uint8_t* dst, uint64_t value) {
    write_u32(dst, (uint32_t)(value >> 32u));
    write_u32(dst + 4u, (uint32_t)(value & UINT64_C(0xffffffff)));
}

static uint16_t read_u16(const uint8_t* src) {
    return (uint16_t)(((uint16_t)src[0] << 8u) | (uint16_t)src[1]);
}

static uint32_t read_u32(const uint8_t* src) {
    return ((uint32_t)src[0] << 24u) |
           ((uint32_t)src[1] << 16u) |
           ((uint32_t)src[2] << 8u) |
           (uint32_t)src[3];
}

static uint64_t read_u64(const uint8_t* src) {
    return ((uint64_t)read_u32(src) << 32u) |
           (uint64_t)read_u32(src + 4u);
}

static bool has_flag(uint8_t flags, uint8_t flag) {
    return (flags & flag) != 0u;
}

bool spw_vspw_tp_is_known_type(spw_vspw_tp_message_type_t type) {
    return type == SPW_VSPW_TP_DATA || type == SPW_VSPW_TP_TIME_CODE ||
           type == SPW_VSPW_TP_LINK_CONTROL ||
           type == SPW_VSPW_TP_KEEPALIVE || type == SPW_VSPW_TP_ACK;
}

static bool validate_flags(const spw_vspw_tp_header_t* header) {
    const uint8_t known_flags =
        SPW_VSPW_TP_FLAG_EOP | SPW_VSPW_TP_FLAG_EEP |
        SPW_VSPW_TP_FLAG_FRAGMENT_START | SPW_VSPW_TP_FLAG_FRAGMENT_END |
        SPW_VSPW_TP_FLAG_ACK_REQUIRED;
    if ((header->flags & (uint8_t)~known_flags) != 0u) {
        return false;
    }
    if (has_flag(header->flags, SPW_VSPW_TP_FLAG_EOP) &&
        has_flag(header->flags, SPW_VSPW_TP_FLAG_EEP)) {
        return false;
    }
    if (header->type != SPW_VSPW_TP_DATA &&
        (has_flag(header->flags, SPW_VSPW_TP_FLAG_EOP) ||
         has_flag(header->flags, SPW_VSPW_TP_FLAG_EEP))) {
        return false;
    }
    if (has_flag(header->flags, SPW_VSPW_TP_FLAG_ACK_REQUIRED) &&
        header->type != SPW_VSPW_TP_DATA &&
        header->type != SPW_VSPW_TP_TIME_CODE) {
        return false;
    }
    return true;
}

static bool validate_fragment(const spw_vspw_tp_header_t* header) {
    const bool start = has_flag(header->flags, SPW_VSPW_TP_FLAG_FRAGMENT_START);
    const bool end = has_flag(header->flags, SPW_VSPW_TP_FLAG_FRAGMENT_END);

    if (header->total_size > SPW_VSPW_TP_MAX_PACKET_SIZE ||
        header->payload_size > SPW_VSPW_TP_MAX_FRAGMENT_PAYLOAD ||
        header->fragment_offset > header->total_size ||
        (uint64_t)header->fragment_offset + header->payload_size >
            header->total_size) {
        return false;
    }

    if (header->type != SPW_VSPW_TP_DATA) {
        return header->fragment_offset == 0u &&
               header->total_size == header->payload_size && !start && !end;
    }

    if (header->total_size == header->payload_size) {
        return header->fragment_offset == 0u && !start && !end;
    }
    if (!start && !end && header->payload_size == 0u) {
        return false;
    }
    if (start && header->fragment_offset != 0u) {
        return false;
    }
    if (end && (uint64_t)header->fragment_offset + header->payload_size !=
                   header->total_size) {
        return false;
    }
    return true;
}

static bool validate_message_shape(const spw_vspw_tp_header_t* header) {
    switch (header->type) {
    case SPW_VSPW_TP_DATA:
        return true;
    case SPW_VSPW_TP_TIME_CODE:
        return header->payload_size == SPW_VSPW_TP_TIME_CODE_PAYLOAD_SIZE &&
               header->total_size == SPW_VSPW_TP_TIME_CODE_PAYLOAD_SIZE;
    case SPW_VSPW_TP_KEEPALIVE:
        return header->payload_size == 0u && header->total_size == 0u &&
               header->message_id == 0u;
    case SPW_VSPW_TP_ACK:
        return header->payload_size == SPW_VSPW_TP_ACK_PAYLOAD_SIZE &&
               header->total_size == SPW_VSPW_TP_ACK_PAYLOAD_SIZE &&
               header->message_id != 0u;
    case SPW_VSPW_TP_LINK_CONTROL:
        return true;
    default:
        return false;
    }
}

bool spw_vspw_tp_validate(const spw_vspw_tp_header_t* header) {
    return header != NULL &&
           header->version_major == SPW_VSPW_TP_VERSION_MAJOR &&
           header->version_minor <= SPW_VSPW_TP_VERSION_MINOR &&
           spw_vspw_tp_is_known_type(header->type) &&
           header->header_size == SPW_VSPW_TP_HEADER_SIZE &&
           header->session_id != 0u && validate_flags(header) &&
           validate_fragment(header) && validate_message_shape(header);
}

bool spw_vspw_tp_encode_header(const spw_vspw_tp_header_t* header,
                               uint8_t* destination,
                               size_t destination_size) {
    if (destination == NULL || destination_size < SPW_VSPW_TP_HEADER_SIZE ||
        !spw_vspw_tp_validate(header)) {
        return false;
    }

    write_u32(destination + 0u, SPW_VSPW_TP_MAGIC);
    destination[4] = header->version_major;
    destination[5] = header->version_minor;
    destination[6] = header->type;
    destination[7] = header->flags;
    write_u16(destination + 8u, header->header_size);
    write_u16(destination + 10u, header->payload_size);
    write_u32(destination + 12u, header->link_id);
    write_u64(destination + 16u, header->session_id);
    write_u32(destination + 24u, header->sequence);
    write_u32(destination + 28u, header->message_id);
    write_u32(destination + 32u, header->fragment_offset);
    write_u32(destination + 36u, header->total_size);
    return true;
}

spw_vspw_tp_decode_result_t spw_vspw_tp_decode_header(
    const uint8_t* source,
    size_t source_size,
    spw_vspw_tp_header_t* out_header) {
    spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
    if (source == NULL || out_header == NULL ||
        source_size < SPW_VSPW_TP_HEADER_SIZE) {
        return SPW_VSPW_TP_DECODE_BUFFER_TOO_SMALL;
    }
    if (read_u32(source + 0u) != SPW_VSPW_TP_MAGIC) {
        return SPW_VSPW_TP_DECODE_BAD_MAGIC;
    }

    header.version_major = source[4];
    header.version_minor = source[5];
    header.type = source[6];
    header.flags = source[7];
    header.header_size = read_u16(source + 8u);
    header.payload_size = read_u16(source + 10u);
    header.link_id = read_u32(source + 12u);
    header.session_id = read_u64(source + 16u);
    header.sequence = read_u32(source + 24u);
    header.message_id = read_u32(source + 28u);
    header.fragment_offset = read_u32(source + 32u);
    header.total_size = read_u32(source + 36u);

    if (header.version_major != SPW_VSPW_TP_VERSION_MAJOR ||
        header.version_minor > SPW_VSPW_TP_VERSION_MINOR) {
        return SPW_VSPW_TP_DECODE_UNSUPPORTED_VERSION;
    }
    if (!spw_vspw_tp_is_known_type(header.type)) {
        return SPW_VSPW_TP_DECODE_UNSUPPORTED_TYPE;
    }
    if (header.header_size != SPW_VSPW_TP_HEADER_SIZE) {
        return SPW_VSPW_TP_DECODE_INVALID_HEADER_SIZE;
    }
    if (header.payload_size > SPW_VSPW_TP_MAX_FRAGMENT_PAYLOAD ||
        source_size < SPW_VSPW_TP_HEADER_SIZE + header.payload_size) {
        return SPW_VSPW_TP_DECODE_INVALID_PAYLOAD_SIZE;
    }
    if (header.session_id == 0u) {
        return SPW_VSPW_TP_DECODE_INVALID_SESSION;
    }
    if (!validate_flags(&header)) {
        return SPW_VSPW_TP_DECODE_INVALID_FLAGS;
    }
    if (!validate_fragment(&header) || !validate_message_shape(&header)) {
        return SPW_VSPW_TP_DECODE_INVALID_FRAGMENT;
    }

    *out_header = header;
    return SPW_VSPW_TP_DECODE_OK;
}

bool spw_vspw_tp_encode_ack_payload(uint64_t acknowledged_session_id,
                                    uint8_t* destination,
                                    size_t destination_size) {
    if (destination == NULL ||
        destination_size < SPW_VSPW_TP_ACK_PAYLOAD_SIZE ||
        acknowledged_session_id == 0u) {
        return false;
    }
    write_u64(destination, acknowledged_session_id);
    return true;
}

bool spw_vspw_tp_decode_ack_payload(const uint8_t* source,
                                    size_t source_size,
                                    uint64_t* acknowledged_session_id) {
    uint64_t value;
    if (source == NULL || acknowledged_session_id == NULL ||
        source_size != SPW_VSPW_TP_ACK_PAYLOAD_SIZE) {
        return false;
    }
    value = read_u64(source);
    if (value == 0u) {
        return false;
    }
    *acknowledged_session_id = value;
    return true;
}
