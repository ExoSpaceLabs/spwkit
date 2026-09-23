// SPDX-License-Identifier: Apache-2.0

#include "backends/ethernet/vspw_engine.h"
#include "profiling/profile.h"

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef union spw_vspw_max_alignment {
    long double long_double_value;
    void* pointer_value;
    uint64_t integer_value;
} spw_vspw_max_alignment_t;

typedef struct spw_vspw_deadline {
    bool infinite;
    uint64_t end_us;
} spw_vspw_deadline_t;

static uint64_t now_us(const spw_vspw_engine_t* engine) {
    return spw_vspw_runtime_now_us(&engine->runtime);
}

static spw_vspw_deadline_t deadline_make(const spw_vspw_engine_t* engine,
                                         spw_timeout_us_t timeout_us) {
    spw_vspw_deadline_t deadline;
    deadline.infinite = timeout_us == SPW_TIMEOUT_INFINITE;
    if (deadline.infinite) {
        deadline.end_us = UINT64_MAX;
    } else {
        const uint64_t now = now_us(engine);
        deadline.end_us = UINT64_MAX - now < timeout_us
                              ? UINT64_MAX
                              : now + timeout_us;
    }
    return deadline;
}

static spw_timeout_us_t deadline_remaining(
    const spw_vspw_engine_t* engine,
    const spw_vspw_deadline_t* deadline) {
    uint64_t now;
    if (deadline->infinite) {
        return SPW_TIMEOUT_INFINITE;
    }
    now = now_us(engine);
    if (now >= deadline->end_us) {
        return SPW_TIMEOUT_IMMEDIATE;
    }
    return deadline->end_us - now;
}

static bool deadline_expired(const spw_vspw_engine_t* engine,
                             const spw_vspw_deadline_t* deadline) {
    return !deadline->infinite && now_us(engine) >= deadline->end_us;
}

static spw_timeout_us_t min_timeout(spw_timeout_us_t lhs,
                                    spw_timeout_us_t rhs) {
    if (lhs == SPW_TIMEOUT_INFINITE) {
        return rhs;
    }
    if (rhs == SPW_TIMEOUT_INFINITE) {
        return lhs;
    }
    return lhs < rhs ? lhs : rhs;
}

static bool valid_terminator(spw_terminator_t terminator) {
    return terminator == SPW_TERMINATOR_EOP || terminator == SPW_TERMINATOR_EEP;
}

static bool valid_time_code(const spw_time_code_t* time_code) {
    return time_code != NULL && time_code->time_count <= 63u &&
           time_code->control_flags <= 3u;
}

static uint8_t terminator_flag(spw_terminator_t terminator) {
    return terminator == SPW_TERMINATOR_EEP ? SPW_VSPW_TP_FLAG_EEP
                                            : SPW_VSPW_TP_FLAG_EOP;
}

static uint32_t take_nonzero(uint32_t* counter) {
    uint32_t value = (*counter)++;
    if (value == 0u) {
        value = (*counter)++;
    }
    return value;
}

static uint64_t make_session_id(const spw_vspw_engine_t* engine) {
    uint64_t value = now_us(engine) ^ engine->config.session_nonce ^
                     ((uint64_t)engine->config.link_id << 16u) ^
                     (uint64_t)(uintptr_t)engine;
    return value == 0u ? 1u : value;
}

static void clear_reassembly(spw_vspw_engine_t* engine) {
    spw_fragment_reassembler_reset(&engine->reassembly);
    engine->reassembly_last_fragment_us = 0u;
}

static void expire_reassembly(spw_vspw_engine_t* engine) {
    const uint64_t now = now_us(engine);
    const uint64_t timeout =
        (uint64_t)engine->config.peer_timeout_ms * UINT64_C(1000);
    if (!engine->reassembly.active ||
        engine->reassembly_last_fragment_us == 0u) {
        return;
    }
    if (now >= engine->reassembly_last_fragment_us &&
        now - engine->reassembly_last_fragment_us >= timeout) {
        clear_reassembly(engine);
    }
}

static void clear_pending_tx(spw_vspw_engine_t* engine) {
    engine->pending_tx_packet_size = 0u;
    engine->pending_tx_terminator = SPW_TERMINATOR_EOP;
    memset(&engine->pending_tx_time_code, 0,
           sizeof(engine->pending_tx_time_code));
    engine->pending_tx_kind = SPW_VSPW_PENDING_NONE;
    engine->pending_tx_message_id = 0u;
    engine->pending_tx_retries = 0u;
    engine->pending_tx_failed = false;
    engine->pending_tx_last_send_us = 0u;
}

static void clear_recent_messages(spw_vspw_engine_t* engine) {
    engine->recent_message_head = 0u;
    engine->recent_message_count = 0u;
}

static void clear_retired_sessions(spw_vspw_engine_t* engine) {
    engine->retired_session_head = 0u;
    engine->retired_session_count = 0u;
}

static bool transport_is_up(const spw_vspw_engine_t* engine) {
    spw_transport_state_t state = SPW_TRANSPORT_STATE_DOWN;
    return spw_transport_provider_get_state(&engine->transport, &state) ==
               SPW_OK &&
           state == SPW_TRANSPORT_STATE_UP;
}

static bool peer_is_current(const spw_vspw_engine_t* engine) {
    const uint64_t now = now_us(engine);
    const uint64_t timeout =
        (uint64_t)engine->config.peer_timeout_ms * UINT64_C(1000);
    if (!engine->peer_seen || engine->last_peer_rx_us == 0u ||
        now < engine->last_peer_rx_us) {
        return false;
    }
    return now - engine->last_peer_rx_us <= timeout;
}

static void mark_peer_lost(spw_vspw_engine_t* engine) {
    if (engine->state != SPW_LINK_ERROR_WAIT) {
        ++engine->statistics.link_errors;
    }
    clear_reassembly(engine);
    engine->state = SPW_LINK_ERROR_WAIT;
}

static void refresh_peer_state(spw_vspw_engine_t* engine) {
    expire_reassembly(engine);
    if (engine->state == SPW_LINK_RUN && !peer_is_current(engine)) {
        mark_peer_lost(engine);
    } else if (engine->state == SPW_LINK_ERROR_WAIT &&
               peer_is_current(engine)) {
        engine->state = SPW_LINK_RUN;
    }
}

static void note_peer_activity(spw_vspw_engine_t* engine) {
    engine->peer_seen = true;
    engine->last_peer_rx_us = now_us(engine);
    if (engine->state == SPW_LINK_CONNECTING ||
        engine->state == SPW_LINK_ERROR_WAIT) {
        engine->state = SPW_LINK_RUN;
    }
}

static bool is_retired_session(const spw_vspw_engine_t* engine,
                               uint64_t session_id) {
    size_t i;
    if (session_id == 0u) {
        return false;
    }
    for (i = 0u; i < engine->retired_session_count; ++i) {
        const size_t index =
            (engine->retired_session_head + i) %
            SPW_VSPW_ENGINE_RETIRED_SESSION_DEPTH;
        if (engine->retired_sessions[index] == session_id) {
            return true;
        }
    }
    return false;
}

static void remember_retired_session(spw_vspw_engine_t* engine,
                                     uint64_t session_id) {
    size_t index;
    if (session_id == 0u || is_retired_session(engine, session_id)) {
        return;
    }
    if (engine->retired_session_count <
        SPW_VSPW_ENGINE_RETIRED_SESSION_DEPTH) {
        index = (engine->retired_session_head +
                 engine->retired_session_count) %
                SPW_VSPW_ENGINE_RETIRED_SESSION_DEPTH;
        engine->retired_sessions[index] = session_id;
        ++engine->retired_session_count;
        return;
    }
    engine->retired_sessions[engine->retired_session_head] = session_id;
    engine->retired_session_head =
        (engine->retired_session_head + 1u) %
        SPW_VSPW_ENGINE_RETIRED_SESSION_DEPTH;
}

static bool recently_delivered(const spw_vspw_engine_t* engine,
                               spw_vspw_tp_message_type_t type,
                               uint32_t message_id) {
    size_t i;
    if (message_id == 0u) {
        return false;
    }
    for (i = 0u; i < engine->recent_message_count; ++i) {
        const size_t index =
            (engine->recent_message_head + i) %
            SPW_VSPW_ENGINE_RECENT_MESSAGE_DEPTH;
        if (engine->recent_messages[index].type == type &&
            engine->recent_messages[index].message_id == message_id) {
            return true;
        }
    }
    return false;
}

static void remember_delivered(spw_vspw_engine_t* engine,
                               spw_vspw_tp_message_type_t type,
                               uint32_t message_id) {
    size_t index;
    if (message_id == 0u || recently_delivered(engine, type, message_id)) {
        return;
    }
    if (engine->recent_message_count <
        SPW_VSPW_ENGINE_RECENT_MESSAGE_DEPTH) {
        index = (engine->recent_message_head + engine->recent_message_count) %
                SPW_VSPW_ENGINE_RECENT_MESSAGE_DEPTH;
        engine->recent_messages[index].type = type;
        engine->recent_messages[index].message_id = message_id;
        ++engine->recent_message_count;
        return;
    }
    engine->recent_messages[engine->recent_message_head].type = type;
    engine->recent_messages[engine->recent_message_head].message_id =
        message_id;
    engine->recent_message_head =
        (engine->recent_message_head + 1u) %
        SPW_VSPW_ENGINE_RECENT_MESSAGE_DEPTH;
}

static bool reset_remote_session(spw_vspw_engine_t* engine,
                                 uint64_t session_id) {
    if (session_id == 0u || is_retired_session(engine, session_id)) {
        return false;
    }
    if (engine->remote_session_id == session_id) {
        note_peer_activity(engine);
        return true;
    }
    if (engine->remote_session_id != 0u) {
        remember_retired_session(engine, engine->remote_session_id);
    }
    engine->remote_session_id = session_id;
    clear_reassembly(engine);
    clear_recent_messages(engine);
    if (engine->pending_tx_kind != SPW_VSPW_PENDING_NONE) {
        engine->pending_tx_retries = 0u;
        engine->pending_tx_failed = false;
        engine->pending_tx_last_send_us = 0u;
    }
    note_peer_activity(engine);
    return true;
}

static spw_result_t send_frame(spw_vspw_engine_t* engine,
                               const uint8_t* bytes,
                               size_t size,
                               spw_timeout_us_t timeout_us) {
    return spw_transport_provider_send(&engine->transport,
                                       &engine->remote_peer,
                                       bytes, size, timeout_us);
}

static spw_result_t pump_one(spw_vspw_engine_t* engine,
                             spw_timeout_us_t timeout_us);
static spw_result_t service_pending_tx(spw_vspw_engine_t* engine);
static spw_result_t send_keepalive(spw_vspw_engine_t* engine,
                                   spw_timeout_us_t timeout_us);

static void maybe_send_keepalive(spw_vspw_engine_t* engine) {
    const uint64_t now = now_us(engine);
    const uint64_t interval =
        (uint64_t)engine->config.keepalive_interval_ms * UINT64_C(1000);
    if (!transport_is_up(engine) || engine->local_session_id == 0u ||
        (engine->state != SPW_LINK_CONNECTING &&
         engine->state != SPW_LINK_RUN &&
         engine->state != SPW_LINK_ERROR_WAIT)) {
        return;
    }
    if (engine->last_keepalive_tx_us == 0u ||
        (now >= engine->last_keepalive_tx_us &&
         now - engine->last_keepalive_tx_us >= interval)) {
        (void)send_keepalive(engine, SPW_TIMEOUT_IMMEDIATE);
    }
}

static spw_result_t wait_virtual_link_delay(spw_vspw_engine_t* engine,
                                            uint64_t delay_us,
                                            spw_timeout_us_t timeout_us) {
    spw_vspw_deadline_t deadline;
    uint64_t target;
    if (delay_us == 0u) {
        return SPW_OK;
    }
    if (timeout_us != SPW_TIMEOUT_INFINITE && timeout_us < delay_us) {
        return SPW_ERR_TIMEOUT;
    }

    deadline = deadline_make(engine, timeout_us);
    target = now_us(engine) + delay_us;
    while (now_us(engine) < target) {
        const uint64_t now = now_us(engine);
        const uint64_t remaining = target > now ? target - now : 0u;
        const spw_timeout_us_t timing_slice =
            remaining == 0u ? SPW_TIMEOUT_IMMEDIATE : remaining;
        const spw_result_t result = pump_one(
            engine,
            min_timeout(deadline_remaining(engine, &deadline), timing_slice));
        if (result != SPW_OK && result != SPW_ERR_TIMEOUT &&
            result != SPW_ERR_RESOURCE_EXHAUSTED) {
            return result;
        }
        if (now_us(engine) >= target) {
            return SPW_OK;
        }
        if (deadline_expired(engine, &deadline)) {
            return SPW_ERR_TIMEOUT;
        }
    }
    return SPW_OK;
}

static spw_result_t send_ack(spw_vspw_engine_t* engine,
                             uint32_t message_id) {
    spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
    if (message_id == 0u) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (engine->remote_session_id == 0u) {
        return SPW_ERR_INVALID_STATE;
    }

    header.type = SPW_VSPW_TP_ACK;
    header.payload_size = SPW_VSPW_TP_ACK_PAYLOAD_SIZE;
    header.link_id = engine->config.link_id;
    header.session_id = engine->local_session_id;
    header.sequence = take_nonzero(&engine->next_sequence);
    header.message_id = message_id;
    header.total_size = SPW_VSPW_TP_ACK_PAYLOAD_SIZE;
    if (!spw_vspw_tp_encode_header(&header, engine->control_frame,
                                   sizeof(engine->control_frame)) ||
        !spw_vspw_tp_encode_ack_payload(
            engine->remote_session_id,
            engine->control_frame + SPW_VSPW_TP_HEADER_SIZE,
            SPW_VSPW_TP_ACK_PAYLOAD_SIZE)) {
        return SPW_ERR_BACKEND;
    }
    return send_frame(engine, engine->control_frame,
                      SPW_VSPW_TP_HEADER_SIZE +
                          SPW_VSPW_TP_ACK_PAYLOAD_SIZE,
                      SPW_TIMEOUT_IMMEDIATE);
}

static spw_result_t send_keepalive(spw_vspw_engine_t* engine,
                                   spw_timeout_us_t timeout_us) {
    spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
    spw_result_t result;
    header.type = SPW_VSPW_TP_KEEPALIVE;
    header.link_id = engine->config.link_id;
    header.session_id = engine->local_session_id;
    header.sequence = take_nonzero(&engine->next_sequence);
    if (!spw_vspw_tp_encode_header(&header, engine->control_frame,
                                   sizeof(engine->control_frame))) {
        return SPW_ERR_BACKEND;
    }
    result = send_frame(engine, engine->control_frame,
                        SPW_VSPW_TP_HEADER_SIZE, timeout_us);
    if (result == SPW_OK) {
        engine->last_keepalive_tx_us = now_us(engine);
    }
    return result;
}

static spw_result_t transmit_pending(spw_vspw_engine_t* engine,
                                     spw_timeout_us_t timeout_us) {
    size_t offset;
    size_t fragment_size;
    bool fragmented;

    if (engine->pending_tx_kind == SPW_VSPW_PENDING_NONE ||
        engine->pending_tx_message_id == 0u) {
        return SPW_OK;
    }

    if (engine->pending_tx_kind == SPW_VSPW_PENDING_TIME_CODE) {
        spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
        spw_result_t result;
        header.type = SPW_VSPW_TP_TIME_CODE;
        header.flags = SPW_VSPW_TP_FLAG_ACK_REQUIRED;
        header.payload_size = SPW_VSPW_TP_TIME_CODE_PAYLOAD_SIZE;
        header.link_id = engine->config.link_id;
        header.session_id = engine->local_session_id;
        header.sequence = take_nonzero(&engine->next_sequence);
        header.message_id = engine->pending_tx_message_id;
        header.total_size = SPW_VSPW_TP_TIME_CODE_PAYLOAD_SIZE;
        if (!spw_vspw_tp_encode_header(&header, engine->tx_frame,
                                       sizeof(engine->tx_frame))) {
            return SPW_ERR_BACKEND;
        }
        engine->tx_frame[SPW_VSPW_TP_HEADER_SIZE] =
            engine->pending_tx_time_code.time_count;
        engine->tx_frame[SPW_VSPW_TP_HEADER_SIZE + 1u] =
            engine->pending_tx_time_code.control_flags;
        result = send_frame(
            engine, engine->tx_frame,
            SPW_VSPW_TP_HEADER_SIZE + SPW_VSPW_TP_TIME_CODE_PAYLOAD_SIZE,
            timeout_us);
        if (result == SPW_OK) {
            engine->pending_tx_last_send_us = now_us(engine);
        }
        return result;
    }

    fragment_size = engine->config.fragment_payload_size;
    fragmented = engine->pending_tx_packet_size > fragment_size;
    offset = 0u;
    do {
        const size_t remaining = engine->pending_tx_packet_size - offset;
        const size_t payload_size =
            fragmented
                ? (fragment_size < remaining ? fragment_size : remaining)
                : remaining;
        spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
        spw_result_t result;

        header.type = SPW_VSPW_TP_DATA;
        header.flags = terminator_flag(engine->pending_tx_terminator) |
                       SPW_VSPW_TP_FLAG_ACK_REQUIRED;
        if (fragmented && offset == 0u) {
            header.flags |= SPW_VSPW_TP_FLAG_FRAGMENT_START;
        }
        if (fragmented &&
            offset + payload_size == engine->pending_tx_packet_size) {
            header.flags |= SPW_VSPW_TP_FLAG_FRAGMENT_END;
        }
        header.payload_size = (uint16_t)payload_size;
        header.link_id = engine->config.link_id;
        header.session_id = engine->local_session_id;
        header.sequence = take_nonzero(&engine->next_sequence);
        header.message_id = engine->pending_tx_message_id;
        header.fragment_offset = (uint32_t)offset;
        header.total_size = (uint32_t)engine->pending_tx_packet_size;

        if (!spw_vspw_tp_encode_header(&header, engine->tx_frame,
                                       sizeof(engine->tx_frame))) {
            return SPW_ERR_INVALID_PACKET;
        }
        if (payload_size != 0u) {
            memcpy(engine->tx_frame + SPW_VSPW_TP_HEADER_SIZE,
                   engine->pending_tx_packet + offset, payload_size);
        }
        result = send_frame(engine, engine->tx_frame,
                            SPW_VSPW_TP_HEADER_SIZE + payload_size,
                            timeout_us);
        if (result != SPW_OK) {
            return result;
        }
        offset += payload_size;
    } while (offset < engine->pending_tx_packet_size);

    engine->pending_tx_last_send_us = now_us(engine);
    return SPW_OK;
}

static spw_result_t service_pending_tx(spw_vspw_engine_t* engine) {
    const uint64_t now = now_us(engine);
    const uint64_t ack_timeout =
        (uint64_t)engine->config.ack_timeout_ms * UINT64_C(1000);
    spw_result_t result;

    if (engine->pending_tx_kind == SPW_VSPW_PENDING_NONE) {
        return SPW_OK;
    }
    if (engine->pending_tx_last_send_us != 0u &&
        now >= engine->pending_tx_last_send_us &&
        now - engine->pending_tx_last_send_us < ack_timeout) {
        return SPW_OK;
    }
    if (engine->pending_tx_retries >= engine->config.max_retries) {
        if (!engine->pending_tx_failed) {
            engine->pending_tx_failed = true;
            ++engine->statistics.dropped_packets;
        }
        mark_peer_lost(engine);
        return SPW_ERR_LINK_UNAVAILABLE;
    }

    result = transmit_pending(engine, SPW_TIMEOUT_IMMEDIATE);
    if (result == SPW_OK) {
        ++engine->pending_tx_retries;
    }
    return result;
}

static spw_result_t process_ack(spw_vspw_engine_t* engine,
                                const spw_vspw_tp_header_t* header,
                                const uint8_t* payload) {
    uint64_t acknowledged_session_id = 0u;
    if (!spw_vspw_tp_decode_ack_payload(payload, header->payload_size,
                                        &acknowledged_session_id)) {
        ++engine->statistics.dropped_packets;
        return SPW_OK;
    }
    if (acknowledged_session_id != engine->local_session_id) {
        return SPW_OK;
    }
    if (engine->pending_tx_kind != SPW_VSPW_PENDING_NONE &&
        header->message_id == engine->pending_tx_message_id) {
        clear_pending_tx(engine);
    }
    return SPW_OK;
}

static spw_result_t process_keepalive(spw_vspw_engine_t* engine,
                                      const spw_vspw_tp_header_t* header) {
    (void)reset_remote_session(engine, header->session_id);
    return SPW_OK;
}

static spw_result_t process_time_code(spw_vspw_engine_t* engine,
                                      const spw_vspw_tp_header_t* header,
                                      const uint8_t* payload) {
    const bool ack_required =
        (header->flags & SPW_VSPW_TP_FLAG_ACK_REQUIRED) != 0u;
    spw_time_code_t time_code;
    size_t index;

    if (ack_required &&
        recently_delivered(engine, SPW_VSPW_TP_TIME_CODE,
                           header->message_id)) {
        (void)send_ack(engine, header->message_id);
        return SPW_OK;
    }
    if (engine->time_code_count == SPW_VSPW_ENGINE_TIME_CODE_QUEUE_DEPTH) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    time_code.time_count = payload[0];
    time_code.control_flags = payload[1];
    if (!valid_time_code(&time_code)) {
        ++engine->statistics.dropped_packets;
        return SPW_OK;
    }

    index = (engine->time_code_head + engine->time_code_count) %
            SPW_VSPW_ENGINE_TIME_CODE_QUEUE_DEPTH;
    engine->time_codes[index] = time_code;
    ++engine->time_code_count;
    if (ack_required) {
        remember_delivered(engine, SPW_VSPW_TP_TIME_CODE,
                           header->message_id);
        (void)send_ack(engine, header->message_id);
    }
    return SPW_OK;
}

static spw_result_t process_data(spw_vspw_engine_t* engine,
                                 const spw_vspw_tp_header_t* header,
                                 const uint8_t* payload) {
    const bool ack_required =
        (header->flags & SPW_VSPW_TP_FLAG_ACK_REQUIRED) != 0u;
    const bool fragmented = header->total_size != header->payload_size;
    const spw_terminator_t terminator =
        (header->flags & SPW_VSPW_TP_FLAG_EEP) != 0u
            ? SPW_TERMINATOR_EEP
            : SPW_TERMINATOR_EOP;

    if (ack_required &&
        recently_delivered(engine, SPW_VSPW_TP_DATA, header->message_id)) {
        (void)send_ack(engine, header->message_id);
        return SPW_OK;
    }

    if (!fragmented) {
        if (engine->pending_packet_valid) {
            return SPW_ERR_RESOURCE_EXHAUSTED;
        }
        if (header->payload_size != 0u) {
            memcpy(engine->pending_packet, payload, header->payload_size);
        }
        engine->pending_packet_size = header->payload_size;
        engine->pending_packet_terminator = terminator;
        engine->pending_packet_valid = true;
        SPW_PROFILE_RX_PROVIDER_BOUNDARY();
        if (ack_required) {
            remember_delivered(engine, SPW_VSPW_TP_DATA,
                               header->message_id);
            (void)send_ack(engine, header->message_id);
        }
        return SPW_OK;
    }

    expire_reassembly(engine);
    {
        const spw_reassembly_result_t result =
            spw_fragment_reassembler_push(&engine->reassembly, header, payload);
        if (result == SPW_REASSEMBLY_INVALID ||
            result == SPW_REASSEMBLY_CONFLICT) {
            ++engine->statistics.dropped_packets;
            return SPW_OK;
        }
        engine->reassembly_last_fragment_us = now_us(engine);
        if (result != SPW_REASSEMBLY_COMPLETE) {
            return SPW_OK;
        }
    }

    if (engine->pending_packet_valid) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    if (engine->reassembly.total_size != 0u) {
        memcpy(engine->pending_packet, engine->reassembly.data,
               engine->reassembly.total_size);
    }
    engine->pending_packet_size = engine->reassembly.total_size;
    engine->pending_packet_terminator =
        (engine->reassembly.terminator_flags & SPW_VSPW_TP_FLAG_EEP) != 0u
            ? SPW_TERMINATOR_EEP
            : SPW_TERMINATOR_EOP;
    engine->pending_packet_valid = true;
    SPW_PROFILE_RX_PROVIDER_BOUNDARY();
    {
        const uint32_t completed_message_id = engine->reassembly.message_id;
        const bool completed_ack_required = engine->reassembly.ack_required;
        clear_reassembly(engine);
        if (completed_ack_required) {
            remember_delivered(engine, SPW_VSPW_TP_DATA,
                               completed_message_id);
            (void)send_ack(engine, completed_message_id);
        }
    }
    return SPW_OK;
}

static spw_result_t pump_one(spw_vspw_engine_t* engine,
                             spw_timeout_us_t timeout_us) {
    spw_timeout_us_t keepalive_slice;
    spw_timeout_us_t service_slice;
    spw_timeout_us_t wait_timeout;
    spw_result_t receive_result;
    size_t received = 0u;
    spw_transport_peer_id_t source = SPW_TRANSPORT_PEER_ID_INITIALIZER;
    spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
    const uint8_t* payload;

    maybe_send_keepalive(engine);
    keepalive_slice =
        (spw_timeout_us_t)engine->config.keepalive_interval_ms * 1000u;
    service_slice = keepalive_slice;
    if (engine->pending_tx_kind != SPW_VSPW_PENDING_NONE) {
        const spw_timeout_us_t ack_slice =
            (spw_timeout_us_t)engine->config.ack_timeout_ms * 1000u;
        service_slice = min_timeout(service_slice, ack_slice);
    }
    wait_timeout = min_timeout(timeout_us, service_slice);

    receive_result = spw_transport_provider_receive(
        &engine->transport, engine->rx_frame, sizeof(engine->rx_frame),
        &received, &source, wait_timeout);

    if (receive_result == SPW_ERR_TIMEOUT) {
        maybe_send_keepalive(engine);
        refresh_peer_state(engine);
        if (timeout_us == SPW_TIMEOUT_INFINITE || wait_timeout < timeout_us) {
            return SPW_OK;
        }
        return SPW_ERR_TIMEOUT;
    }
    if (receive_result != SPW_OK) {
        return receive_result;
    }
    if (!spw_transport_peer_id_equal(&source, &engine->remote_peer)) {
        return SPW_OK;
    }

    if (spw_vspw_tp_decode_header(engine->rx_frame, received, &header) !=
            SPW_VSPW_TP_DECODE_OK ||
        received != SPW_VSPW_TP_HEADER_SIZE + header.payload_size) {
        ++engine->statistics.dropped_packets;
        return SPW_OK;
    }
    if (header.link_id != engine->config.link_id) {
        return SPW_OK;
    }

    payload = engine->rx_frame + SPW_VSPW_TP_HEADER_SIZE;
    if (header.type == SPW_VSPW_TP_KEEPALIVE) {
        return process_keepalive(engine, &header);
    }
    if (engine->remote_session_id == 0u ||
        header.session_id != engine->remote_session_id) {
        return SPW_OK;
    }
    note_peer_activity(engine);

    switch (header.type) {
    case SPW_VSPW_TP_DATA:
        if (header.total_size > SPW_VSPW_ENGINE_MAX_PACKET_SIZE) {
            ++engine->statistics.dropped_packets;
            return SPW_OK;
        }
        return process_data(engine, &header, payload);
    case SPW_VSPW_TP_TIME_CODE:
        return process_time_code(engine, &header, payload);
    case SPW_VSPW_TP_ACK:
        return process_ack(engine, &header, payload);
    case SPW_VSPW_TP_KEEPALIVE:
    case SPW_VSPW_TP_LINK_CONTROL:
    default:
        return SPW_OK;
    }
}

static spw_result_t ensure_peer(spw_vspw_engine_t* engine,
                                spw_timeout_us_t timeout_us) {
    spw_vspw_deadline_t deadline;
    refresh_peer_state(engine);
    if (engine->state == SPW_LINK_RUN && peer_is_current(engine)) {
        return SPW_OK;
    }
    if (engine->state != SPW_LINK_CONNECTING &&
        engine->state != SPW_LINK_ERROR_WAIT &&
        engine->state != SPW_LINK_RUN) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }

    deadline = deadline_make(engine, timeout_us);
    for (;;) {
        spw_result_t result;
        maybe_send_keepalive(engine);
        result = pump_one(engine, deadline_remaining(engine, &deadline));
        refresh_peer_state(engine);
        if (engine->state == SPW_LINK_RUN && peer_is_current(engine)) {
            return SPW_OK;
        }
        if (result != SPW_OK && result != SPW_ERR_TIMEOUT &&
            result != SPW_ERR_RESOURCE_EXHAUSTED) {
            return result;
        }
        if (deadline_expired(engine, &deadline) ||
            timeout_us == SPW_TIMEOUT_IMMEDIATE) {
            return SPW_ERR_LINK_UNAVAILABLE;
        }
    }
}

static spw_result_t wait_for_tx_slot(spw_vspw_engine_t* engine,
                                     spw_timeout_us_t timeout_us) {
    spw_vspw_deadline_t deadline;
    if (engine->pending_tx_kind == SPW_VSPW_PENDING_NONE) {
        return SPW_OK;
    }

    deadline = deadline_make(engine, timeout_us);
    for (;;) {
        spw_result_t service_result = service_pending_tx(engine);
        spw_result_t pump_result;
        spw_timeout_us_t retry_slice;
        if (engine->pending_tx_kind == SPW_VSPW_PENDING_NONE) {
            return SPW_OK;
        }
        if (service_result == SPW_ERR_LINK_UNAVAILABLE) {
            return service_result;
        }
        if (service_result != SPW_OK && service_result != SPW_ERR_TIMEOUT) {
            return service_result;
        }

        retry_slice =
            (spw_timeout_us_t)engine->config.ack_timeout_ms * 1000u;
        pump_result = pump_one(
            engine,
            min_timeout(deadline_remaining(engine, &deadline), retry_slice));
        if (pump_result != SPW_OK && pump_result != SPW_ERR_TIMEOUT &&
            pump_result != SPW_ERR_RESOURCE_EXHAUSTED) {
            return pump_result;
        }
        if (engine->pending_tx_kind == SPW_VSPW_PENDING_NONE) {
            return SPW_OK;
        }
        if (deadline_expired(engine, &deadline) ||
            timeout_us == SPW_TIMEOUT_IMMEDIATE) {
            return SPW_ERR_TIMEOUT;
        }
    }
}

static void clear_runtime_state(spw_vspw_engine_t* engine) {
    clear_reassembly(engine);
    clear_pending_tx(engine);
    clear_recent_messages(engine);
    clear_retired_sessions(engine);
    engine->pending_packet_valid = false;
    engine->pending_packet_size = 0u;
    engine->pending_packet_terminator = SPW_TERMINATOR_EOP;
    engine->time_code_head = 0u;
    engine->time_code_count = 0u;
    engine->peer_seen = false;
    engine->remote_session_id = 0u;
    engine->last_peer_rx_us = 0u;
    engine->last_keepalive_tx_us = 0u;
}

spw_result_t spw_vspw_engine_init(
    spw_vspw_engine_t* engine,
    const spw_vspw_engine_config_t* config,
    const spw_transport_provider_t* transport,
    const spw_transport_peer_id_t* remote_peer,
    const spw_vspw_runtime_t* runtime) {
    size_t mtu = 0u;

    if (engine == NULL || config == NULL || transport == NULL ||
        remote_peer == NULL || runtime == NULL ||
        !spw_transport_provider_valid(transport) ||
        !spw_vspw_runtime_valid(runtime) || remote_peer->size == 0u ||
        remote_peer->size > SPW_TRANSPORT_PEER_ID_MAX_SIZE ||
        config->link_id == 0u || config->fragment_payload_size < 256u ||
        config->fragment_payload_size > SPW_VSPW_TP_MAX_FRAGMENT_PAYLOAD ||
        config->max_retries == 0u || config->ack_timeout_ms == 0u ||
        config->keepalive_interval_ms == 0u ||
        config->peer_timeout_ms <= config->keepalive_interval_ms) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (spw_transport_provider_get_mtu(transport, &mtu) != SPW_OK ||
        mtu > SPW_VSPW_TP_MAX_CARRIER_FRAME ||
        mtu < SPW_VSPW_TP_HEADER_SIZE + config->fragment_payload_size ||
        mtu < SPW_VSPW_TP_HEADER_SIZE + SPW_VSPW_TP_ACK_PAYLOAD_SIZE) {
        return SPW_ERR_UNSUPPORTED;
    }

    memset(engine, 0, sizeof(*engine));
    engine->config = *config;
    engine->transport = *transport;
    engine->remote_peer = *remote_peer;
    engine->runtime = *runtime;
    engine->virtual_timing.link_bps = config->virtual_link_bps;
    engine->virtual_timing.latency_us = config->virtual_latency_us;
    engine->state = SPW_LINK_READY;
    engine->next_sequence = 1u;
    engine->next_message_id = 1u;
    engine->pending_packet_terminator = SPW_TERMINATOR_EOP;
    engine->pending_tx_terminator = SPW_TERMINATOR_EOP;
    spw_fragment_reassembler_init(
        &engine->reassembly, engine->reassembly_data,
        sizeof(engine->reassembly_data), engine->reassembly_coverage,
        sizeof(engine->reassembly_coverage) /
            sizeof(engine->reassembly_coverage[0]));
    return SPW_OK;
}

spw_result_t spw_vspw_engine_start(spw_vspw_engine_t* engine) {
    spw_result_t result;
    if (engine == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    result = spw_transport_provider_start(&engine->transport);
    if (result != SPW_OK) {
        return result;
    }

    clear_runtime_state(engine);
    engine->next_sequence = 1u;
    engine->next_message_id = 1u;
    engine->local_session_id = make_session_id(engine);
    engine->state = SPW_LINK_CONNECTING;

    result = send_keepalive(engine, SPW_TIMEOUT_IMMEDIATE);
    return result == SPW_ERR_TIMEOUT ? SPW_OK : result;
}

spw_result_t spw_vspw_engine_stop(spw_vspw_engine_t* engine) {
    if (engine == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    engine->state = SPW_LINK_READY;
    clear_runtime_state(engine);
    return spw_transport_provider_stop(&engine->transport);
}

spw_result_t spw_vspw_engine_reset(spw_vspw_engine_t* engine) {
    if (engine == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    engine->state = SPW_LINK_ERROR_RESET;
    clear_runtime_state(engine);
    return spw_transport_provider_reset(&engine->transport);
}

spw_result_t spw_vspw_engine_get_link_state(
    spw_vspw_engine_t* engine,
    spw_link_state_t* out_state) {
    if (engine == NULL || out_state == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (engine->state == SPW_LINK_CONNECTING ||
        engine->state == SPW_LINK_RUN ||
        engine->state == SPW_LINK_ERROR_WAIT) {
        maybe_send_keepalive(engine);
        (void)pump_one(engine, SPW_TIMEOUT_IMMEDIATE);
        (void)service_pending_tx(engine);
        refresh_peer_state(engine);
    }
    *out_state = engine->state;
    return SPW_OK;
}

spw_result_t spw_vspw_engine_get_capabilities(
    const spw_vspw_engine_t* engine,
    spw_capabilities_t* out_capabilities) {
    if (engine == NULL || out_capabilities == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    out_capabilities->bits = SPW_CAP_EEP | SPW_CAP_TIME_CODE |
                             SPW_CAP_STATISTICS | SPW_CAP_RATE_CONTROL;
    out_capabilities->max_packet_size = SPW_VSPW_ENGINE_MAX_PACKET_SIZE;
    out_capabilities->tx_queue_depth = 1u;
    out_capabilities->rx_queue_depth = 1u;
    out_capabilities->buffer_alignment = alignof(spw_vspw_max_alignment_t);
    return SPW_OK;
}

spw_result_t spw_vspw_engine_send(
    spw_vspw_engine_t* engine,
    const spw_packet_t* packet,
    spw_timeout_us_t timeout_us) {
    spw_vspw_deadline_t deadline;
    spw_result_t result;

    if (engine == NULL || packet == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    SPW_PROFILE_TX_BACKEND_ENTRY();
    if ((packet->length != 0u && packet->data == NULL) ||
        packet->length > SPW_VSPW_ENGINE_MAX_PACKET_SIZE ||
        !valid_terminator(packet->terminator)) {
        return SPW_ERR_INVALID_PACKET;
    }

    deadline = deadline_make(engine, timeout_us);
    result = ensure_peer(engine, deadline_remaining(engine, &deadline));
    if (result != SPW_OK) {
        return result;
    }
    result = wait_for_tx_slot(engine, deadline_remaining(engine, &deadline));
    if (result != SPW_OK) {
        return result;
    }

    result = wait_virtual_link_delay(
        engine,
        spw_virtual_link_delay_us(&engine->virtual_timing,
                                  SPW_VIRTUAL_LINK_EVENT_DATA,
                                  packet->length),
        deadline_remaining(engine, &deadline));
    if (result != SPW_OK) {
        return result;
    }

    if (packet->length != 0u) {
        memcpy(engine->pending_tx_packet, packet->data, packet->length);
    }
    engine->pending_tx_packet_size = packet->length;
    engine->pending_tx_terminator = packet->terminator;
    engine->pending_tx_kind = SPW_VSPW_PENDING_DATA;
    engine->pending_tx_message_id =
        take_nonzero(&engine->next_message_id);
    engine->pending_tx_retries = 0u;
    engine->pending_tx_last_send_us = 0u;

    (void)send_keepalive(engine, SPW_TIMEOUT_IMMEDIATE);
    SPW_PROFILE_TX_PROVIDER_ENTRY();
    result = transmit_pending(engine, deadline_remaining(engine, &deadline));
    if (result != SPW_OK) {
        clear_pending_tx(engine);
        return result;
    }
    SPW_PROFILE_TX_PROVIDER_BOUNDARY();

    ++engine->statistics.tx_packets;
    engine->statistics.tx_bytes += packet->length;
    if (engine->pending_tx_terminator == SPW_TERMINATOR_EEP) {
        ++engine->statistics.eep_packets;
    }
    return SPW_OK;
}

spw_result_t spw_vspw_engine_receive(
    spw_vspw_engine_t* engine,
    spw_packet_t* packet,
    spw_timeout_us_t timeout_us) {
    spw_vspw_deadline_t deadline;
    spw_result_t peer_result;

    if (engine == NULL || packet == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    deadline = deadline_make(engine, timeout_us);
    peer_result = ensure_peer(engine, deadline_remaining(engine, &deadline));
    if (peer_result != SPW_OK && !engine->pending_packet_valid) {
        return peer_result;
    }

    while (!engine->pending_packet_valid) {
        spw_result_t service_result = service_pending_tx(engine);
        spw_result_t result;
        if (service_result == SPW_ERR_LINK_UNAVAILABLE) {
            return service_result;
        }
        result = pump_one(engine, deadline_remaining(engine, &deadline));
        if (result != SPW_OK && result != SPW_ERR_RESOURCE_EXHAUSTED &&
            result != SPW_ERR_TIMEOUT) {
            return result;
        }
        if (!engine->pending_packet_valid &&
            (deadline_expired(engine, &deadline) ||
             timeout_us == SPW_TIMEOUT_IMMEDIATE)) {
            refresh_peer_state(engine);
            return engine->state == SPW_LINK_ERROR_WAIT
                       ? SPW_ERR_LINK_UNAVAILABLE
                       : SPW_ERR_TIMEOUT;
        }
    }

    packet->length = engine->pending_packet_size;
    packet->terminator = engine->pending_packet_terminator;
    if (packet->capacity < engine->pending_packet_size ||
        (engine->pending_packet_size != 0u && packet->data == NULL)) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }
    if (engine->pending_packet_size != 0u) {
        memcpy(packet->data, engine->pending_packet,
               engine->pending_packet_size);
    }
    engine->pending_packet_valid = false;
    SPW_PROFILE_RX_PROVIDER_RETURN();
    ++engine->statistics.rx_packets;
    engine->statistics.rx_bytes += packet->length;
    SPW_PROFILE_RX_BACKEND_RETURN();
    return SPW_OK;
}

spw_result_t spw_vspw_engine_send_time_code(
    spw_vspw_engine_t* engine,
    const spw_time_code_t* time_code,
    spw_timeout_us_t timeout_us) {
    spw_vspw_deadline_t deadline;
    spw_result_t result;

    if (engine == NULL || !valid_time_code(time_code)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    deadline = deadline_make(engine, timeout_us);
    result = ensure_peer(engine, deadline_remaining(engine, &deadline));
    if (result != SPW_OK) {
        return result;
    }
    result = wait_for_tx_slot(engine, deadline_remaining(engine, &deadline));
    if (result != SPW_OK) {
        return result;
    }

    result = wait_virtual_link_delay(
        engine,
        spw_virtual_link_delay_us(&engine->virtual_timing,
                                  SPW_VIRTUAL_LINK_EVENT_TIME_CODE,
                                  SPW_VSPW_TP_TIME_CODE_PAYLOAD_SIZE),
        deadline_remaining(engine, &deadline));
    if (result != SPW_OK) {
        return result;
    }

    engine->pending_tx_time_code = *time_code;
    engine->pending_tx_kind = SPW_VSPW_PENDING_TIME_CODE;
    engine->pending_tx_message_id =
        take_nonzero(&engine->next_message_id);
    engine->pending_tx_retries = 0u;
    engine->pending_tx_last_send_us = 0u;

    (void)send_keepalive(engine, SPW_TIMEOUT_IMMEDIATE);
    result = transmit_pending(engine, deadline_remaining(engine, &deadline));
    if (result != SPW_OK) {
        clear_pending_tx(engine);
        return result;
    }
    ++engine->statistics.tx_time_codes;
    return SPW_OK;
}

spw_result_t spw_vspw_engine_receive_time_code(
    spw_vspw_engine_t* engine,
    spw_time_code_t* time_code,
    spw_timeout_us_t timeout_us) {
    spw_vspw_deadline_t deadline;
    spw_result_t peer_result;

    if (engine == NULL || time_code == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    deadline = deadline_make(engine, timeout_us);
    peer_result = ensure_peer(engine, deadline_remaining(engine, &deadline));
    if (peer_result != SPW_OK && engine->time_code_count == 0u) {
        return peer_result;
    }

    while (engine->time_code_count == 0u) {
        spw_result_t service_result = service_pending_tx(engine);
        spw_result_t result;
        if (service_result == SPW_ERR_LINK_UNAVAILABLE) {
            return service_result;
        }
        result = pump_one(engine, deadline_remaining(engine, &deadline));
        if (result != SPW_OK && result != SPW_ERR_RESOURCE_EXHAUSTED &&
            result != SPW_ERR_TIMEOUT) {
            return result;
        }
        if (engine->time_code_count == 0u &&
            (deadline_expired(engine, &deadline) ||
             timeout_us == SPW_TIMEOUT_IMMEDIATE)) {
            refresh_peer_state(engine);
            return engine->state == SPW_LINK_ERROR_WAIT
                       ? SPW_ERR_LINK_UNAVAILABLE
                       : SPW_ERR_TIMEOUT;
        }
    }

    *time_code = engine->time_codes[engine->time_code_head];
    engine->time_code_head =
        (engine->time_code_head + 1u) %
        SPW_VSPW_ENGINE_TIME_CODE_QUEUE_DEPTH;
    --engine->time_code_count;
    ++engine->statistics.rx_time_codes;
    return SPW_OK;
}

spw_result_t spw_vspw_engine_get_statistics(
    const spw_vspw_engine_t* engine,
    spw_statistics_t* out_statistics) {
    if (engine == NULL || out_statistics == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_statistics = engine->statistics;
    return SPW_OK;
}

spw_result_t spw_vspw_engine_clear_statistics(spw_vspw_engine_t* engine) {
    if (engine == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    memset(&engine->statistics, 0, sizeof(engine->statistics));
    return SPW_OK;
}
