// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_VSPW_TP_H
#define SPWKIT_VSPW_TP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPW_VSPW_TP_MAGIC UINT32_C(0x56535057)
#define SPW_VSPW_TP_VERSION_MAJOR 1u
#define SPW_VSPW_TP_VERSION_MINOR 0u
#define SPW_VSPW_TP_HEADER_SIZE 40u
#define SPW_VSPW_TP_MAX_UDP_PAYLOAD 65507u
#define SPW_VSPW_TP_MAX_FRAGMENT_PAYLOAD \
    (SPW_VSPW_TP_MAX_UDP_PAYLOAD - SPW_VSPW_TP_HEADER_SIZE)
#define SPW_VSPW_TP_MAX_PACKET_SIZE (16u * 1024u * 1024u)
#define SPW_VSPW_TP_TIME_CODE_PAYLOAD_SIZE 2u
#define SPW_VSPW_TP_ACK_PAYLOAD_SIZE 8u

typedef uint8_t spw_vspw_tp_message_type_t;
enum {
    SPW_VSPW_TP_DATA = 1u,
    SPW_VSPW_TP_TIME_CODE = 2u,
    SPW_VSPW_TP_LINK_CONTROL = 3u,
    SPW_VSPW_TP_KEEPALIVE = 4u,
    SPW_VSPW_TP_ACK = 5u
};

enum {
    SPW_VSPW_TP_FLAG_NONE = 0u,
    SPW_VSPW_TP_FLAG_EOP = 1u << 0u,
    SPW_VSPW_TP_FLAG_EEP = 1u << 1u,
    SPW_VSPW_TP_FLAG_FRAGMENT_START = 1u << 2u,
    SPW_VSPW_TP_FLAG_FRAGMENT_END = 1u << 3u,
    SPW_VSPW_TP_FLAG_ACK_REQUIRED = 1u << 4u
};

typedef struct spw_vspw_tp_header {
    uint8_t version_major;
    uint8_t version_minor;
    spw_vspw_tp_message_type_t type;
    uint8_t flags;
    uint16_t header_size;
    uint16_t payload_size;
    uint32_t link_id;
    uint64_t session_id;
    uint32_t sequence;
    uint32_t message_id;
    uint32_t fragment_offset;
    uint32_t total_size;
} spw_vspw_tp_header_t;

#define SPW_VSPW_TP_HEADER_INITIALIZER \
    { SPW_VSPW_TP_VERSION_MAJOR, SPW_VSPW_TP_VERSION_MINOR, \
      SPW_VSPW_TP_DATA, SPW_VSPW_TP_FLAG_NONE, SPW_VSPW_TP_HEADER_SIZE, \
      0u, 0u, 0u, 0u, 0u, 0u, 0u }

typedef uint8_t spw_vspw_tp_decode_result_t;
enum {
    SPW_VSPW_TP_DECODE_OK = 0u,
    SPW_VSPW_TP_DECODE_BUFFER_TOO_SMALL,
    SPW_VSPW_TP_DECODE_BAD_MAGIC,
    SPW_VSPW_TP_DECODE_UNSUPPORTED_VERSION,
    SPW_VSPW_TP_DECODE_UNSUPPORTED_TYPE,
    SPW_VSPW_TP_DECODE_INVALID_HEADER_SIZE,
    SPW_VSPW_TP_DECODE_INVALID_PAYLOAD_SIZE,
    SPW_VSPW_TP_DECODE_INVALID_SESSION,
    SPW_VSPW_TP_DECODE_INVALID_FLAGS,
    SPW_VSPW_TP_DECODE_INVALID_FRAGMENT
};

bool spw_vspw_tp_is_known_type(spw_vspw_tp_message_type_t type);
bool spw_vspw_tp_validate(const spw_vspw_tp_header_t* header);
bool spw_vspw_tp_encode_header(const spw_vspw_tp_header_t* header,
                               uint8_t* destination,
                               size_t destination_size);
spw_vspw_tp_decode_result_t spw_vspw_tp_decode_header(
    const uint8_t* source,
    size_t source_size,
    spw_vspw_tp_header_t* out_header);
bool spw_vspw_tp_encode_ack_payload(uint64_t acknowledged_session_id,
                                    uint8_t* destination,
                                    size_t destination_size);
bool spw_vspw_tp_decode_ack_payload(const uint8_t* source,
                                    size_t source_size,
                                    uint64_t* acknowledged_session_id);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_VSPW_TP_H */
