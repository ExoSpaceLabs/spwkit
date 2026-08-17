// SPDX-License-Identifier: Apache-2.0
#include "cuse/vspw_cuse_record.h"

#define VSPW_CUSE_RECORD_RESERVED_SIZE 4u

static uint32_t read_be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static void write_be32(uint8_t* p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static vspw_cuse_record_result_t validate_header(
    const vspw_cuse_record_header_t* header) {
    if (header == NULL) {
        return VSPW_CUSE_RECORD_INVALID_ARGUMENT;
    }
    if (header->type != VSPW_CUSE_RECORD_DATA &&
        header->type != VSPW_CUSE_RECORD_TIME_CODE) {
        return VSPW_CUSE_RECORD_INVALID_TYPE;
    }
    if ((header->flags & (uint8_t)~VSPW_CUSE_RECORD_KNOWN_FLAGS) != 0u) {
        return VSPW_CUSE_RECORD_INVALID_FLAGS;
    }
    if (header->payload_size > VSPW_CUSE_RECORD_MAX_PAYLOAD) {
        return VSPW_CUSE_RECORD_INVALID_SIZE;
    }
    if (header->type == VSPW_CUSE_RECORD_TIME_CODE) {
        if (header->flags != 0u || header->payload_size != 2u) {
            return VSPW_CUSE_RECORD_INVALID_PAYLOAD;
        }
    }
    return VSPW_CUSE_RECORD_OK;
}

vspw_cuse_record_result_t vspw_cuse_record_encode_header(
    const vspw_cuse_record_header_t* header,
    uint8_t out[VSPW_CUSE_RECORD_HEADER_SIZE]) {
    vspw_cuse_record_result_t result;
    size_t i;

    if (out == NULL) {
        return VSPW_CUSE_RECORD_INVALID_ARGUMENT;
    }
    result = validate_header(header);
    if (result != VSPW_CUSE_RECORD_OK) {
        return result;
    }

    write_be32(out + 0u, VSPW_CUSE_RECORD_MAGIC);
    out[4] = VSPW_CUSE_RECORD_VERSION;
    out[5] = header->type;
    out[6] = header->flags;
    out[7] = 0u;
    write_be32(out + 8u, header->payload_size);
    for (i = 0u; i < VSPW_CUSE_RECORD_RESERVED_SIZE; ++i) {
        out[12u + i] = 0u;
    }
    return VSPW_CUSE_RECORD_OK;
}

vspw_cuse_record_result_t vspw_cuse_record_decode_header(
    const uint8_t in[VSPW_CUSE_RECORD_HEADER_SIZE],
    vspw_cuse_record_header_t* out_header) {
    uint32_t magic;
    size_t i;

    if (in == NULL || out_header == NULL) {
        return VSPW_CUSE_RECORD_INVALID_ARGUMENT;
    }
    magic = read_be32(in + 0u);
    if (magic != VSPW_CUSE_RECORD_MAGIC) {
        return VSPW_CUSE_RECORD_INVALID_MAGIC;
    }
    if (in[4] != VSPW_CUSE_RECORD_VERSION) {
        return VSPW_CUSE_RECORD_INVALID_VERSION;
    }
    if (in[7] != 0u) {
        return VSPW_CUSE_RECORD_INVALID_PAYLOAD;
    }
    for (i = 0u; i < VSPW_CUSE_RECORD_RESERVED_SIZE; ++i) {
        if (in[12u + i] != 0u) {
            return VSPW_CUSE_RECORD_INVALID_PAYLOAD;
        }
    }

    out_header->type = in[5];
    out_header->flags = in[6];
    out_header->payload_size = read_be32(in + 8u);
    return validate_header(out_header);
}

vspw_cuse_record_result_t vspw_cuse_record_validate_payload(
    const vspw_cuse_record_header_t* header,
    const uint8_t* payload,
    size_t payload_size) {
    vspw_cuse_record_result_t result = validate_header(header);
    if (result != VSPW_CUSE_RECORD_OK) {
        return result;
    }
    if (payload_size != header->payload_size) {
        return VSPW_CUSE_RECORD_INVALID_SIZE;
    }
    if (payload_size != 0u && payload == NULL) {
        return VSPW_CUSE_RECORD_INVALID_ARGUMENT;
    }
    if (header->type == VSPW_CUSE_RECORD_TIME_CODE) {
        if (payload[0] > 63u || (payload[1] & 0xfcu) != 0u) {
            return VSPW_CUSE_RECORD_INVALID_PAYLOAD;
        }
    }
    return VSPW_CUSE_RECORD_OK;
}
