// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_VSPW_DEVICE_PROTOCOL_H
#define SPWKIT_VSPW_DEVICE_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * VSPD is the private, versioned protocol between a hosted SpWKit device
 * backend and vspwd. It is not a public application API and is deliberately
 * distinct from VSPW-TP, which is the distributed UDP transport protocol.
 */
#define VSPD_MAGIC UINT32_C(0x56535044) /* "VSPD" */
#define VSPD_VERSION_MAJOR 1u
#define VSPD_VERSION_MINOR 3u
#define VSPD_HEADER_SIZE 40u

/* Conservative per-SOCK_SEQPACKET record bound. Logical DATA can span records. */
#define VSPD_MAX_FRAME_PAYLOAD (32u * 1024u)
#define VSPD_MAX_LOGICAL_PACKET (1024u * 1024u)

/* Message types. Requests and responses use the same type plus RESPONSE flag. */
#define VSPD_MSG_HELLO            1u
#define VSPD_MSG_ATTACH           2u
#define VSPD_MSG_DETACH           3u
#define VSPD_MSG_START            4u
#define VSPD_MSG_STOP             5u
#define VSPD_MSG_RESET            6u
#define VSPD_MSG_GET_LINK_STATE   7u
#define VSPD_MSG_GET_CAPABILITIES 8u
#define VSPD_MSG_DATA_TX          9u
#define VSPD_MSG_DATA_RX          10u
#define VSPD_MSG_TIME_CODE_TX     11u
#define VSPD_MSG_TIME_CODE_RX     12u
#define VSPD_MSG_GET_STATISTICS        13u
#define VSPD_MSG_CLEAR_STATISTICS      14u
#define VSPD_MSG_LINK_STATE_EVENT      15u
#define VSPD_MSG_GET_SERVER_INFO       16u
#define VSPD_MSG_GET_PORT_INFO         17u
#define VSPD_MSG_GET_PORT_STATISTICS   18u
#define VSPD_MSG_CLEAR_PORT_STATISTICS 19u
#define VSPD_MSG_SUBSCRIBE_PORT        20u
#define VSPD_MSG_UNSUBSCRIBE_PORT      21u
#define VSPD_MSG_PORT_SNAPSHOT_EVENT   22u

/* Generic/message-shape flags. */
#define VSPD_FLAG_RESPONSE       0x01u
#define VSPD_FLAG_FRAGMENT_START 0x02u
#define VSPD_FLAG_FRAGMENT_END   0x04u
#define VSPD_FLAG_EOP            0x08u
#define VSPD_FLAG_EEP            0x10u
#define VSPD_FLAG_KNOWN_MASK     0x1fu

/* Fixed payload sizes for non-DATA messages. */
#define VSPD_HELLO_PAYLOAD_SIZE        4u
#define VSPD_LINK_STATE_PAYLOAD_SIZE   4u
#define VSPD_CAPABILITIES_PAYLOAD_SIZE 24u
#define VSPD_TIME_CODE_PAYLOAD_SIZE    2u
#define VSPD_STATISTICS_PAYLOAD_SIZE   72u
#define VSPD_SERVER_INFO_PAYLOAD_SIZE  20u
#define VSPD_PORT_INFO_PAYLOAD_SIZE    16u
#define VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE \
    (VSPD_PORT_INFO_PAYLOAD_SIZE + VSPD_STATISTICS_PAYLOAD_SIZE)

#define VSPD_PORT_INFO_ATTACHED      UINT32_C(0x01)
#define VSPD_PORT_INFO_STARTED       UINT32_C(0x02)
#define VSPD_PORT_INFO_RESET_LATCHED UINT32_C(0x04)
#define VSPD_PORT_INFO_EVER_ATTACHED UINT32_C(0x08)
#define VSPD_PORT_INFO_BRIDGED       UINT32_C(0x10)
#define VSPD_PORT_INFO_KNOWN_MASK    UINT32_C(0x1f)

/*
 * Fixed protocol status values. They intentionally mirror the current public
 * spw_result_t meanings, but are defined independently so the wire protocol
 * never depends on process-local enum/typedef representation.
 */
#define VSPD_STATUS_OK                 INT32_C(0)
#define VSPD_STATUS_INVALID_ARGUMENT   INT32_C(-1)
#define VSPD_STATUS_INVALID_STATE      INT32_C(-2)
#define VSPD_STATUS_TIMEOUT            INT32_C(-3)
#define VSPD_STATUS_UNSUPPORTED        INT32_C(-4)
#define VSPD_STATUS_RESOURCE_EXHAUSTED INT32_C(-5)
#define VSPD_STATUS_LINK_UNAVAILABLE   INT32_C(-6)
#define VSPD_STATUS_BUFFER_TOO_SMALL   INT32_C(-7)
#define VSPD_STATUS_INVALID_PACKET     INT32_C(-8)
#define VSPD_STATUS_BACKEND            INT32_C(-9)

/* Wire link-state values are explicit protocol constants, not native structs. */
#define VSPD_LINK_ERROR_RESET 0u
#define VSPD_LINK_ERROR_WAIT  1u
#define VSPD_LINK_READY       2u
#define VSPD_LINK_STARTED     3u
#define VSPD_LINK_CONNECTING  4u
#define VSPD_LINK_RUN         5u

/* Private codec results. Hosted backend/daemon code translates as appropriate. */
typedef int32_t vspd_codec_result_t;
#define VSPD_CODEC_OK                  ((vspd_codec_result_t)0)
#define VSPD_CODEC_INVALID_ARGUMENT    ((vspd_codec_result_t)-1)
#define VSPD_CODEC_TRUNCATED           ((vspd_codec_result_t)-2)
#define VSPD_CODEC_INVALID_MAGIC       ((vspd_codec_result_t)-3)
#define VSPD_CODEC_UNSUPPORTED_VERSION ((vspd_codec_result_t)-4)
#define VSPD_CODEC_INVALID_HEADER      ((vspd_codec_result_t)-5)
#define VSPD_CODEC_INVALID_TYPE        ((vspd_codec_result_t)-6)
#define VSPD_CODEC_INVALID_FLAGS       ((vspd_codec_result_t)-7)
#define VSPD_CODEC_INVALID_SIZE        ((vspd_codec_result_t)-8)
#define VSPD_CODEC_INVALID_SHAPE       ((vspd_codec_result_t)-9)

/* Host-order representation of the fixed 40-byte network-order wire header. */
typedef struct vspd_header {
    uint32_t magic;
    uint8_t version_major;
    uint8_t version_minor;
    uint8_t type;
    uint8_t flags;
    uint16_t header_size;
    uint16_t reserved;
    uint32_t payload_size;
    uint32_t request_id;
    uint32_t port_id;
    uint32_t message_id;
    uint32_t fragment_offset;
    uint32_t total_size;
    int32_t status;
} vspd_header_t;

/* Payload helpers are encoded explicitly; native struct layout never goes on wire. */
typedef struct vspd_capabilities_payload {
    uint64_t bits;
    uint32_t max_packet_size;
    uint32_t tx_queue_depth;
    uint32_t rx_queue_depth;
    uint32_t buffer_alignment;
} vspd_capabilities_payload_t;

typedef struct vspd_server_info_payload {
    uint32_t port_count;
    uint32_t client_capacity;
    uint32_t packet_queue_depth;
    uint32_t time_code_queue_depth;
    uint32_t max_logical_packet;
} vspd_server_info_payload_t;

typedef struct vspd_port_info_payload {
    uint32_t flags;
    uint32_t link_state;
    uint32_t packet_queue_count;
    uint32_t time_code_queue_count;
} vspd_port_info_payload_t;

typedef struct vspd_statistics_payload {
    uint64_t tx_packets;
    uint64_t rx_packets;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t tx_time_codes;
    uint64_t rx_time_codes;
    uint64_t eep_packets;
    uint64_t link_errors;
    uint64_t dropped_packets;
} vspd_statistics_payload_t;

typedef struct vspd_port_snapshot_payload {
    vspd_port_info_payload_t info;
    vspd_statistics_payload_t statistics;
} vspd_port_snapshot_payload_t;

vspd_codec_result_t vspd_encode_header(const vspd_header_t* header,
                                       uint8_t out[VSPD_HEADER_SIZE]);

vspd_codec_result_t vspd_decode_header(const uint8_t* data,
                                       size_t size,
                                       vspd_header_t* out_header);

/* Validate header semantics plus the exact record size/payload shape. */
vspd_codec_result_t vspd_validate_frame(const uint8_t* frame,
                                        size_t frame_size,
                                        vspd_header_t* out_header);

void vspd_encode_u32_payload(uint32_t value, uint8_t out[4]);
uint32_t vspd_decode_u32_payload(const uint8_t in[4]);

void vspd_encode_capabilities(const vspd_capabilities_payload_t* value,
                              uint8_t out[VSPD_CAPABILITIES_PAYLOAD_SIZE]);
void vspd_decode_capabilities(const uint8_t in[VSPD_CAPABILITIES_PAYLOAD_SIZE],
                              vspd_capabilities_payload_t* out);

void vspd_encode_server_info(const vspd_server_info_payload_t* value,
                             uint8_t out[VSPD_SERVER_INFO_PAYLOAD_SIZE]);
void vspd_decode_server_info(const uint8_t in[VSPD_SERVER_INFO_PAYLOAD_SIZE],
                             vspd_server_info_payload_t* out);

void vspd_encode_port_info(const vspd_port_info_payload_t* value,
                           uint8_t out[VSPD_PORT_INFO_PAYLOAD_SIZE]);
void vspd_decode_port_info(const uint8_t in[VSPD_PORT_INFO_PAYLOAD_SIZE],
                           vspd_port_info_payload_t* out);

void vspd_encode_statistics(const vspd_statistics_payload_t* value,
                            uint8_t out[VSPD_STATISTICS_PAYLOAD_SIZE]);
void vspd_decode_statistics(const uint8_t in[VSPD_STATISTICS_PAYLOAD_SIZE],
                            vspd_statistics_payload_t* out);

void vspd_encode_port_snapshot(
    const vspd_port_snapshot_payload_t* value,
    uint8_t out[VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE]);
void vspd_decode_port_snapshot(
    const uint8_t in[VSPD_PORT_SNAPSHOT_PAYLOAD_SIZE],
    vspd_port_snapshot_payload_t* out);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_VSPW_DEVICE_PROTOCOL_H */
