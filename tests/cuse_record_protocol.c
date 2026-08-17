// SPDX-License-Identifier: Apache-2.0
#include "cuse/vspw_cuse_record.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void test_data_round_trip(void) {
    const vspw_cuse_record_header_t input = {
        VSPW_CUSE_RECORD_DATA,
        VSPW_CUSE_RECORD_FLAG_EEP,
        5u};
    vspw_cuse_record_header_t decoded = {0u, 0u, 0u};
    uint8_t wire[VSPW_CUSE_RECORD_HEADER_SIZE];
    const uint8_t payload[5] = {'h', 'e', 'l', 'l', 'o'};

    assert(vspw_cuse_record_encode_header(&input, wire) == VSPW_CUSE_RECORD_OK);
    assert(wire[0] == 'S' && wire[1] == 'P' && wire[2] == 'W' && wire[3] == 'R');
    assert(wire[4] == VSPW_CUSE_RECORD_VERSION);
    assert(wire[5] == VSPW_CUSE_RECORD_DATA);
    assert(wire[6] == VSPW_CUSE_RECORD_FLAG_EEP);
    assert(wire[7] == 0u);
    assert(wire[8] == 0u && wire[9] == 0u && wire[10] == 0u && wire[11] == 5u);
    assert(vspw_cuse_record_decode_header(wire, &decoded) == VSPW_CUSE_RECORD_OK);
    assert(decoded.type == input.type);
    assert(decoded.flags == input.flags);
    assert(decoded.payload_size == input.payload_size);
    assert(vspw_cuse_record_validate_payload(&decoded, payload, sizeof(payload)) ==
           VSPW_CUSE_RECORD_OK);
}

static void test_zero_length_eop(void) {
    const vspw_cuse_record_header_t header = {
        VSPW_CUSE_RECORD_DATA,
        0u,
        0u};
    uint8_t wire[VSPW_CUSE_RECORD_HEADER_SIZE];

    assert(vspw_cuse_record_encode_header(&header, wire) == VSPW_CUSE_RECORD_OK);
    assert(vspw_cuse_record_validate_payload(&header, NULL, 0u) ==
           VSPW_CUSE_RECORD_OK);
}

static void test_time_code(void) {
    const vspw_cuse_record_header_t header = {
        VSPW_CUSE_RECORD_TIME_CODE,
        0u,
        2u};
    const uint8_t valid[2] = {63u, 3u};
    const uint8_t bad_count[2] = {64u, 0u};
    const uint8_t bad_flags[2] = {7u, 4u};

    assert(vspw_cuse_record_validate_payload(&header, valid, sizeof(valid)) ==
           VSPW_CUSE_RECORD_OK);
    assert(vspw_cuse_record_validate_payload(&header, bad_count, sizeof(bad_count)) ==
           VSPW_CUSE_RECORD_INVALID_PAYLOAD);
    assert(vspw_cuse_record_validate_payload(&header, bad_flags, sizeof(bad_flags)) ==
           VSPW_CUSE_RECORD_INVALID_PAYLOAD);
}

static void test_malformed_headers(void) {
    vspw_cuse_record_header_t decoded;
    uint8_t wire[VSPW_CUSE_RECORD_HEADER_SIZE] = {
        'S', 'P', 'W', 'R', VSPW_CUSE_RECORD_VERSION,
        VSPW_CUSE_RECORD_DATA, 0u, 0u,
        0u, 0u, 0u, 1u,
        0u, 0u, 0u, 0u};

    wire[0] = 'X';
    assert(vspw_cuse_record_decode_header(wire, &decoded) ==
           VSPW_CUSE_RECORD_INVALID_MAGIC);
    wire[0] = 'S';

    wire[4] = (uint8_t)(VSPW_CUSE_RECORD_VERSION + 1u);
    assert(vspw_cuse_record_decode_header(wire, &decoded) ==
           VSPW_CUSE_RECORD_INVALID_VERSION);
    wire[4] = VSPW_CUSE_RECORD_VERSION;

    wire[5] = 99u;
    assert(vspw_cuse_record_decode_header(wire, &decoded) ==
           VSPW_CUSE_RECORD_INVALID_TYPE);
    wire[5] = VSPW_CUSE_RECORD_DATA;

    wire[6] = 0x80u;
    assert(vspw_cuse_record_decode_header(wire, &decoded) ==
           VSPW_CUSE_RECORD_INVALID_FLAGS);
    wire[6] = 0u;

    wire[12] = 1u;
    assert(vspw_cuse_record_decode_header(wire, &decoded) ==
           VSPW_CUSE_RECORD_INVALID_PAYLOAD);
}

static void test_bounds(void) {
    vspw_cuse_record_header_t header = {
        VSPW_CUSE_RECORD_DATA,
        0u,
        VSPW_CUSE_RECORD_MAX_PAYLOAD + 1u};
    uint8_t wire[VSPW_CUSE_RECORD_HEADER_SIZE];

    assert(vspw_cuse_record_encode_header(&header, wire) ==
           VSPW_CUSE_RECORD_INVALID_SIZE);

    header.payload_size = 4u;
    assert(vspw_cuse_record_validate_payload(&header, (const uint8_t*)"abc", 3u) ==
           VSPW_CUSE_RECORD_INVALID_SIZE);
}

int main(void) {
    test_data_round_trip();
    test_zero_length_eop();
    test_time_code();
    test_malformed_headers();
    test_bounds();
    return 0;
}
