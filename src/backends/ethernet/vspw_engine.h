// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_VSPW_ENGINE_H
#define SPWKIT_VSPW_ENGINE_H

#include "backends/ethernet/fragment_reassembler.h"
#include "backends/ethernet/transport_provider.h"
#include "backends/ethernet/virtual_link_timing.h"
#include "backends/ethernet/vspw_runtime.h"
#include "backends/ethernet/vspw_tp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <spwkit/config.h>
#include <spwkit/types.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SPW_VSPW_ENGINE_MAX_PACKET_SIZE = 1024u * 1024u,
    SPW_VSPW_ENGINE_TIME_CODE_QUEUE_DEPTH = 8u,
    SPW_VSPW_ENGINE_RECENT_MESSAGE_DEPTH = 32u,
    SPW_VSPW_ENGINE_RETIRED_SESSION_DEPTH = 8u,
    SPW_VSPW_ENGINE_CONTROL_FRAME_SIZE = 64u
};

typedef struct spw_vspw_engine_config {
    uint32_t link_id;
    uint16_t fragment_payload_size;
    uint16_t max_retries;
    uint32_t ack_timeout_ms;
    uint32_t keepalive_interval_ms;
    uint32_t peer_timeout_ms;
    uint64_t virtual_link_bps;
    uint32_t virtual_latency_us;
    uint64_t session_nonce;
} spw_vspw_engine_config_t;

typedef uint8_t spw_vspw_pending_tx_kind_t;
enum {
    SPW_VSPW_PENDING_NONE = 0u,
    SPW_VSPW_PENDING_DATA = 1u,
    SPW_VSPW_PENDING_TIME_CODE = 2u
};

typedef struct spw_vspw_delivered_key {
    spw_vspw_tp_message_type_t type;
    uint32_t message_id;
} spw_vspw_delivered_key_t;

typedef struct spw_vspw_engine {
    spw_vspw_engine_config_t config;
    spw_virtual_link_timing_t virtual_timing;
    spw_transport_provider_t transport;
    spw_transport_peer_id_t remote_peer;
    spw_vspw_runtime_t runtime;

    spw_link_state_t state;
    spw_statistics_t statistics;
    uint32_t next_sequence;
    uint32_t next_message_id;

    uint8_t tx_frame[SPW_VSPW_TP_MAX_CARRIER_FRAME];
    uint8_t rx_frame[SPW_VSPW_TP_MAX_CARRIER_FRAME];
    uint8_t control_frame[SPW_VSPW_ENGINE_CONTROL_FRAME_SIZE];

    uint8_t reassembly_data[SPW_VSPW_ENGINE_MAX_PACKET_SIZE];
    uint64_t reassembly_coverage[
        SPW_FRAGMENT_COVERAGE_WORDS(SPW_VSPW_ENGINE_MAX_PACKET_SIZE)];
    spw_fragment_reassembler_t reassembly;
    uint64_t reassembly_last_fragment_us;

    uint8_t pending_packet[SPW_VSPW_ENGINE_MAX_PACKET_SIZE];
    size_t pending_packet_size;
    spw_terminator_t pending_packet_terminator;
    bool pending_packet_valid;

    spw_time_code_t time_codes[SPW_VSPW_ENGINE_TIME_CODE_QUEUE_DEPTH];
    size_t time_code_head;
    size_t time_code_count;

    uint8_t pending_tx_packet[SPW_VSPW_ENGINE_MAX_PACKET_SIZE];
    size_t pending_tx_packet_size;
    spw_terminator_t pending_tx_terminator;
    spw_time_code_t pending_tx_time_code;
    spw_vspw_pending_tx_kind_t pending_tx_kind;
    uint32_t pending_tx_message_id;
    uint16_t pending_tx_retries;
    bool pending_tx_failed;
    uint64_t pending_tx_last_send_us;

    spw_vspw_delivered_key_t
        recent_messages[SPW_VSPW_ENGINE_RECENT_MESSAGE_DEPTH];
    size_t recent_message_head;
    size_t recent_message_count;

    uint64_t retired_sessions[SPW_VSPW_ENGINE_RETIRED_SESSION_DEPTH];
    size_t retired_session_head;
    size_t retired_session_count;

    uint64_t local_session_id;
    uint64_t remote_session_id;
    bool peer_seen;
    uint64_t last_peer_rx_us;
    uint64_t last_keepalive_tx_us;
} spw_vspw_engine_t;

spw_result_t spw_vspw_engine_init(
    spw_vspw_engine_t* engine,
    const spw_vspw_engine_config_t* config,
    const spw_transport_provider_t* transport,
    const spw_transport_peer_id_t* remote_peer,
    const spw_vspw_runtime_t* runtime);

spw_result_t spw_vspw_engine_start(spw_vspw_engine_t* engine);
spw_result_t spw_vspw_engine_stop(spw_vspw_engine_t* engine);
spw_result_t spw_vspw_engine_reset(spw_vspw_engine_t* engine);

spw_result_t spw_vspw_engine_get_link_state(
    spw_vspw_engine_t* engine,
    spw_link_state_t* out_state);
spw_result_t spw_vspw_engine_get_capabilities(
    const spw_vspw_engine_t* engine,
    spw_capabilities_t* out_capabilities);

spw_result_t spw_vspw_engine_send(
    spw_vspw_engine_t* engine,
    const spw_packet_t* packet,
    spw_timeout_us_t timeout_us);
spw_result_t spw_vspw_engine_receive(
    spw_vspw_engine_t* engine,
    spw_packet_t* packet,
    spw_timeout_us_t timeout_us);
spw_result_t spw_vspw_engine_send_time_code(
    spw_vspw_engine_t* engine,
    const spw_time_code_t* time_code,
    spw_timeout_us_t timeout_us);
spw_result_t spw_vspw_engine_receive_time_code(
    spw_vspw_engine_t* engine,
    spw_time_code_t* time_code,
    spw_timeout_us_t timeout_us);

spw_result_t spw_vspw_engine_get_statistics(
    const spw_vspw_engine_t* engine,
    spw_statistics_t* out_statistics);
spw_result_t spw_vspw_engine_clear_statistics(spw_vspw_engine_t* engine);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_VSPW_ENGINE_H */
