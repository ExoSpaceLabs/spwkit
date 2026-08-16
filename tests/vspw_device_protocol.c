// SPDX-License-Identifier: Apache-2.0

#include "backends/device/vspw_device_protocol.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static vspd_header_t header_for(uint8_t type,
                                uint8_t flags,
                                uint32_t payload_size,
                                uint32_t request_id,
                                uint32_t port_id) {
    vspd_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = VSPD_MAGIC;
    header.version_major = VSPD_VERSION_MAJOR;
    header.version_minor = VSPD_VERSION_MINOR;
    header.type = type;
    header.flags = flags;
    header.header_size = VSPD_HEADER_SIZE;
    header.payload_size = payload_size;
    header.request_id = request_id;
    header.port_id = port_id;
    return header;
}

static void encode_frame(const vspd_header_t* header,
                         const uint8_t* payload,
                         uint8_t* frame,
                         size_t frame_capacity) {
    assert(frame_capacity >= VSPD_HEADER_SIZE + header->payload_size);
    assert(vspd_encode_header(header, frame) == VSPD_CODEC_OK);
    if (header->payload_size != 0u) {
        assert(payload != NULL);
        memcpy(frame + VSPD_HEADER_SIZE, payload, header->payload_size);
    }
}

static void test_golden_data_vector(void) {
    static const uint8_t expected_header[VSPD_HEADER_SIZE] = {
        0x56u, 0x53u, 0x50u, 0x44u,
        0x01u, 0x00u, 0x09u, 0x0eu,
        0x00u, 0x28u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x04u,
        0x00u, 0x00u, 0x00u, 0x11u,
        0x00u, 0x00u, 0x00u, 0x02u,
        0x00u, 0x00u, 0x00u, 0x07u,
        0x00u, 0x00u, 0x00u, 0x00u,
        0x00u, 0x00u, 0x00u, 0x04u,
        0x00u, 0x00u, 0x00u, 0x00u,
    };
    static const uint8_t payload[] = {0x53u, 0x50u, 0x57u, 0x4bu};
    uint8_t frame[VSPD_HEADER_SIZE + sizeof(payload)];
    vspd_header_t header = header_for(
        VSPD_MSG_DATA_TX,
        VSPD_FLAG_FRAGMENT_START | VSPD_FLAG_FRAGMENT_END | VSPD_FLAG_EOP,
        (uint32_t)sizeof(payload),
        0x11u,
        2u);
    vspd_header_t decoded;

    header.message_id = 7u;
    header.total_size = (uint32_t)sizeof(payload);
    encode_frame(&header, payload, frame, sizeof(frame));
    assert(memcmp(frame, expected_header, VSPD_HEADER_SIZE) == 0);
    assert(vspd_validate_frame(frame, sizeof(frame), &decoded) == VSPD_CODEC_OK);
    assert(decoded.type == VSPD_MSG_DATA_TX);
    assert(decoded.request_id == 0x11u);
    assert(decoded.port_id == 2u);
    assert(decoded.message_id == 7u);
    assert(decoded.total_size == sizeof(payload));
    assert(memcmp(frame + VSPD_HEADER_SIZE, payload, sizeof(payload)) == 0);
}

static void test_hello_and_responses(void) {
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_HELLO_PAYLOAD_SIZE];
    const uint8_t hello[VSPD_HELLO_PAYLOAD_SIZE] = {
        VSPD_VERSION_MAJOR, VSPD_VERSION_MINOR, 0u, 0u};
    vspd_header_t header = header_for(
        VSPD_MSG_HELLO, 0u, VSPD_HELLO_PAYLOAD_SIZE, 1u, 0u);

    encode_frame(&header, hello, frame, sizeof(frame));
    assert(vspd_validate_frame(frame, sizeof(frame), NULL) == VSPD_CODEC_OK);

    header.flags = VSPD_FLAG_RESPONSE;
    header.status = 0;
    encode_frame(&header, hello, frame, sizeof(frame));
    assert(vspd_validate_frame(frame, sizeof(frame), NULL) == VSPD_CODEC_OK);

    header.status = -4;
    header.payload_size = 0u;
    encode_frame(&header, NULL, frame, sizeof(frame));
    assert(vspd_validate_frame(frame, VSPD_HEADER_SIZE, NULL) == VSPD_CODEC_OK);
}

static void test_fragment_shapes(void) {
    static uint8_t frame[VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD];
    static uint8_t payload[VSPD_MAX_FRAME_PAYLOAD];
    vspd_header_t header = header_for(
        VSPD_MSG_DATA_RX,
        VSPD_FLAG_FRAGMENT_START,
        VSPD_MAX_FRAME_PAYLOAD,
        0u,
        3u);

    header.message_id = 100u;
    header.total_size = 40000u;
    encode_frame(&header, payload, frame, sizeof(frame));
    assert(vspd_validate_frame(
               frame, VSPD_HEADER_SIZE + VSPD_MAX_FRAME_PAYLOAD, NULL) ==
           VSPD_CODEC_OK);

    header.flags = VSPD_FLAG_FRAGMENT_END | VSPD_FLAG_EEP;
    header.payload_size = 40000u - VSPD_MAX_FRAME_PAYLOAD;
    header.fragment_offset = VSPD_MAX_FRAME_PAYLOAD;
    encode_frame(&header, payload, frame, sizeof(frame));
    assert(vspd_validate_frame(
               frame, VSPD_HEADER_SIZE + header.payload_size, NULL) ==
           VSPD_CODEC_OK);

    header.flags = VSPD_FLAG_FRAGMENT_END | VSPD_FLAG_EOP | VSPD_FLAG_EEP;
    encode_frame(&header, payload, frame, sizeof(frame));
    assert(vspd_validate_frame(
               frame, VSPD_HEADER_SIZE + header.payload_size, NULL) ==
           VSPD_CODEC_INVALID_SHAPE);

    header.flags = VSPD_FLAG_EOP;
    encode_frame(&header, payload, frame, sizeof(frame));
    assert(vspd_validate_frame(
               frame, VSPD_HEADER_SIZE + header.payload_size, NULL) ==
           VSPD_CODEC_INVALID_SHAPE);

    header.flags = VSPD_FLAG_FRAGMENT_END | VSPD_FLAG_EOP;
    header.total_size = VSPD_MAX_LOGICAL_PACKET + 1u;
    encode_frame(&header, payload, frame, sizeof(frame));
    assert(vspd_validate_frame(
               frame, VSPD_HEADER_SIZE + header.payload_size, NULL) ==
           VSPD_CODEC_INVALID_SHAPE);
}

static void test_zero_length_packet(void) {
    uint8_t frame[VSPD_HEADER_SIZE];
    vspd_header_t header = header_for(
        VSPD_MSG_DATA_TX,
        VSPD_FLAG_FRAGMENT_START | VSPD_FLAG_FRAGMENT_END | VSPD_FLAG_EEP,
        0u,
        9u,
        1u);
    header.message_id = 1u;
    header.total_size = 0u;
    encode_frame(&header, NULL, frame, sizeof(frame));
    assert(vspd_validate_frame(frame, sizeof(frame), NULL) == VSPD_CODEC_OK);
}

static void test_control_payloads(void) {
    uint8_t frame[VSPD_HEADER_SIZE + VSPD_STATISTICS_PAYLOAD_SIZE];
    uint8_t payload[VSPD_STATISTICS_PAYLOAD_SIZE];
    vspd_header_t header;
    vspd_capabilities_payload_t capabilities = {
        UINT64_C(0x1122334455667788), 1048576u, 8u, 9u, 64u};
    vspd_capabilities_payload_t decoded_capabilities;
    vspd_statistics_payload_t statistics = {
        1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u};
    vspd_statistics_payload_t decoded_statistics;

    vspd_encode_capabilities(&capabilities, payload);
    memset(&decoded_capabilities, 0, sizeof(decoded_capabilities));
    vspd_decode_capabilities(payload, &decoded_capabilities);
    assert(memcmp(&capabilities, &decoded_capabilities, sizeof(capabilities)) == 0);

    header = header_for(VSPD_MSG_GET_CAPABILITIES,
                        VSPD_FLAG_RESPONSE,
                        VSPD_CAPABILITIES_PAYLOAD_SIZE,
                        14u,
                        2u);
    encode_frame(&header, payload, frame, sizeof(frame));
    assert(vspd_validate_frame(
               frame, VSPD_HEADER_SIZE + VSPD_CAPABILITIES_PAYLOAD_SIZE, NULL) ==
           VSPD_CODEC_OK);

    vspd_encode_statistics(&statistics, payload);
    memset(&decoded_statistics, 0, sizeof(decoded_statistics));
    vspd_decode_statistics(payload, &decoded_statistics);
    assert(memcmp(&statistics, &decoded_statistics, sizeof(statistics)) == 0);

    header = header_for(VSPD_MSG_GET_STATISTICS,
                        VSPD_FLAG_RESPONSE,
                        VSPD_STATISTICS_PAYLOAD_SIZE,
                        15u,
                        2u);
    encode_frame(&header, payload, frame, sizeof(frame));
    assert(vspd_validate_frame(
               frame, VSPD_HEADER_SIZE + VSPD_STATISTICS_PAYLOAD_SIZE, NULL) ==
           VSPD_CODEC_OK);

    header = header_for(VSPD_MSG_GET_LINK_STATE,
                        VSPD_FLAG_RESPONSE,
                        VSPD_LINK_STATE_PAYLOAD_SIZE,
                        16u,
                        2u);
    vspd_encode_u32_payload(VSPD_LINK_RUN, payload);
    encode_frame(&header, payload, frame, sizeof(frame));
    assert(vspd_validate_frame(
               frame, VSPD_HEADER_SIZE + VSPD_LINK_STATE_PAYLOAD_SIZE, NULL) ==
           VSPD_CODEC_OK);
    assert(vspd_decode_u32_payload(payload) == VSPD_LINK_RUN);
}

static void test_malformed_frames(void) {
    uint8_t frame[VSPD_HEADER_SIZE + 4u];
    const uint8_t hello[4] = {VSPD_VERSION_MAJOR, VSPD_VERSION_MINOR, 0u, 0u};
    vspd_header_t header = header_for(VSPD_MSG_HELLO, 0u, 4u, 1u, 0u);

    encode_frame(&header, hello, frame, sizeof(frame));
    assert(vspd_validate_frame(frame, VSPD_HEADER_SIZE - 1u, NULL) ==
           VSPD_CODEC_TRUNCATED);

    frame[0] = 0u;
    assert(vspd_validate_frame(frame, sizeof(frame), NULL) ==
           VSPD_CODEC_INVALID_MAGIC);
    encode_frame(&header, hello, frame, sizeof(frame));

    frame[5] = 1u;
    assert(vspd_validate_frame(frame, sizeof(frame), NULL) ==
           VSPD_CODEC_UNSUPPORTED_VERSION);
    encode_frame(&header, hello, frame, sizeof(frame));

    frame[7] = 0x80u;
    assert(vspd_validate_frame(frame, sizeof(frame), NULL) ==
           VSPD_CODEC_INVALID_FLAGS);
    encode_frame(&header, hello, frame, sizeof(frame));

    frame[10] = 1u;
    assert(vspd_validate_frame(frame, sizeof(frame), NULL) ==
           VSPD_CODEC_INVALID_HEADER);
    encode_frame(&header, hello, frame, sizeof(frame));

    assert(vspd_validate_frame(frame, sizeof(frame) - 1u, NULL) ==
           VSPD_CODEC_TRUNCATED);

    header.request_id = 0u;
    encode_frame(&header, hello, frame, sizeof(frame));
    assert(vspd_validate_frame(frame, sizeof(frame), NULL) ==
           VSPD_CODEC_INVALID_SHAPE);

    header = header_for(VSPD_MSG_TIME_CODE_RX, 0u, 2u, 0u, 1u);
    frame[VSPD_HEADER_SIZE + 0u] = 64u;
    frame[VSPD_HEADER_SIZE + 1u] = 0u;
    assert(vspd_encode_header(&header, frame) == VSPD_CODEC_OK);
    assert(vspd_validate_frame(frame, VSPD_HEADER_SIZE + 2u, NULL) ==
           VSPD_CODEC_INVALID_SHAPE);

    header = header_for(VSPD_MSG_LINK_STATE_EVENT, 0u, 4u, 0u, 1u);
    vspd_encode_u32_payload(VSPD_LINK_RUN + 1u, frame + VSPD_HEADER_SIZE);
    assert(vspd_encode_header(&header, frame) == VSPD_CODEC_OK);
    assert(vspd_validate_frame(frame, VSPD_HEADER_SIZE + 4u, NULL) ==
           VSPD_CODEC_INVALID_SHAPE);
}

int main(void) {
    test_golden_data_vector();
    test_hello_and_responses();
    test_fragment_shapes();
    test_zero_length_packet();
    test_control_payloads();
    test_malformed_frames();
    return 0;
}
