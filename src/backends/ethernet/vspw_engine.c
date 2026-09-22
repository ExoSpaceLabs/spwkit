// SPDX-License-Identifier: Apache-2.0

#include "backends/ethernet/vspw_engine.h"
#include "profiling/profile.h"

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint64_t now_us(const spw_vspw_engine_t* backend) {
    return spw_vspw_runtime_now_us(&backend->runtime);
}

static spw_vspw_deadline_t deadline_make(
    const spw_vspw_engine_t* backend,
    spw_timeout_us_t timeout_us) {
    spw_vspw_deadline_t deadline;
    deadline.infinite = timeout_us == SPW_TIMEOUT_INFINITE;
    if (deadline.infinite) {
        deadline.end_us = UINT64_MAX;
    } else {
        const uint64_t now = now_us(backend);
        deadline.end_us = UINT64_MAX - now < timeout_us
                              ? UINT64_MAX
                              : now + timeout_us;
    }
    return deadline;
}

static spw_timeout_us_t deadline_remaining(
    const spw_vspw_engine_t* backend,
    const spw_vspw_deadline_t* deadline) {
    uint64_t now;
    if (deadline->infinite) {
        return SPW_TIMEOUT_INFINITE;
    }
    now = now_us(backend);
    if (now >= deadline->end_us) {
        return SPW_TIMEOUT_IMMEDIATE;
    }
    return deadline->end_us - now;
}

static bool deadline_expired(const spw_vspw_engine_t* backend,
                             const spw_vspw_deadline_t* deadline) {
    return !deadline->infinite && now_us(backend) >= deadline->end_us;
}

static bool valid_terminator(spw_terminator_t terminator) {
    return terminator == SPW_TERMINATOR_EOP || terminator == SPW_TERMINATOR_EEP;
}

static bool valid_time_code(const spw_time_code_t* time_code) {
    return time_code != NULL && time_code->time_count <= 63u &&
           time_code->control_flags <= 3u;
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

static uint64_t make_session_id(const spw_vspw_engine_t* backend) {
    uint64_t value = now_us(backend) ^ backend->config.session_salt ^
        ((uint64_t)backend->config.link_id << 32u) ^
        (uint64_t)(uintptr_t)backend;
    return value == 0u ? 1u : value;
}

static void clear_reassembly(spw_vspw_engine_t* backend) {
    spw_fragment_reassembler_reset(&backend->reassembly);
    backend->reassembly_last_fragment_us = 0u;
}

static void expire_reassembly(spw_vspw_engine_t* backend) {
    const uint64_t now = now_us(backend);
    const uint64_t timeout_us_value =
        (uint64_t)backend->config.peer_timeout_ms * UINT64_C(1000);
    if (!backend->reassembly.active || backend->reassembly_last_fragment_us == 0u) {
        return;
    }
    if (now >= backend->reassembly_last_fragment_us &&
        now - backend->reassembly_last_fragment_us >= timeout_us_value) {
        clear_reassembly(backend);
    }
}

static void clear_pending_tx(spw_vspw_engine_t* backend) {
    backend->pending_tx_packet_size = 0u;
    backend->pending_tx_terminator = SPW_TERMINATOR_EOP;
    memset(&backend->pending_tx_time_code, 0, sizeof(backend->pending_tx_time_code));
    backend->pending_tx_kind = SPW_VSPW_PENDING_NONE;
    backend->pending_tx_message_id = 0u;
    backend->pending_tx_retries = 0u;
    backend->pending_tx_failed = false;
    backend->pending_tx_last_send_us = 0u;
}

static void clear_recent_messages(spw_vspw_engine_t* backend) {
    backend->recent_message_head = 0u;
    backend->recent_message_count = 0u;
}

static void clear_retired_sessions(spw_vspw_engine_t* backend) {
    backend->retired_session_head = 0u;
    backend->retired_session_count = 0u;
}

static void clear_reordered_message(spw_vspw_engine_t* backend) {
    backend->reordered_message_size = 0u;
    backend->reordered_message_valid = false;
}

static bool peer_is_current(const spw_vspw_engine_t* backend) {
    const uint64_t now = now_us(backend);
    const uint64_t timeout =
        (uint64_t)backend->config.peer_timeout_ms * UINT64_C(1000);
    if (!backend->peer_seen || backend->last_peer_rx_us == 0u ||
        now < backend->last_peer_rx_us) {
        return false;
    }
    return now - backend->last_peer_rx_us <= timeout;
}

static void mark_peer_lost(spw_vspw_engine_t* backend) {
    if (backend->state != SPW_LINK_ERROR_WAIT) {
        ++backend->statistics.link_errors;
    }
    clear_reassembly(backend);
    backend->state = SPW_LINK_ERROR_WAIT;
}

static void refresh_peer_state(spw_vspw_engine_t* backend) {
    expire_reassembly(backend);
    if (backend->state == SPW_LINK_RUN && !peer_is_current(backend)) {
        mark_peer_lost(backend);
    } else if (backend->state == SPW_LINK_ERROR_WAIT && peer_is_current(backend)) {
        backend->state = SPW_LINK_RUN;
    }
}

static void note_peer_activity(spw_vspw_engine_t* backend) {
    backend->peer_seen = true;
    backend->last_peer_rx_us = now_us(backend);
    if (backend->state == SPW_LINK_CONNECTING ||
        backend->state == SPW_LINK_ERROR_WAIT) {
        backend->state = SPW_LINK_RUN;
    }
}

static bool is_retired_session(const spw_vspw_engine_t* backend,
                               uint64_t session_id) {
    size_t i;
    if (session_id == 0u) {
        return false;
    }
    for (i = 0u; i < backend->retired_session_count; ++i) {
        const size_t index =
            (backend->retired_session_head + i) % SPW_VSPW_RETIRED_SESSION_DEPTH;
        if (backend->retired_sessions[index] == session_id) {
            return true;
        }
    }
    return false;
}

static void remember_retired_session(spw_vspw_engine_t* backend,
                                     uint64_t session_id) {
    size_t index;
    if (session_id == 0u || is_retired_session(backend, session_id)) {
        return;
    }
    if (backend->retired_session_count < SPW_VSPW_RETIRED_SESSION_DEPTH) {
        index = (backend->retired_session_head + backend->retired_session_count) %
                SPW_VSPW_RETIRED_SESSION_DEPTH;
        backend->retired_sessions[index] = session_id;
        ++backend->retired_session_count;
        return;
    }
    backend->retired_sessions[backend->retired_session_head] = session_id;
    backend->retired_session_head =
        (backend->retired_session_head + 1u) % SPW_VSPW_RETIRED_SESSION_DEPTH;
}

static bool recently_delivered(const spw_vspw_engine_t* backend,
                               spw_vspw_tp_message_type_t type,
                               uint32_t message_id) {
    size_t i;
    if (message_id == 0u) {
        return false;
    }
    for (i = 0u; i < backend->recent_message_count; ++i) {
        const size_t index =
            (backend->recent_message_head + i) % SPW_VSPW_RECENT_MESSAGE_DEPTH;
        if (backend->recent_messages[index].type == type &&
            backend->recent_messages[index].message_id == message_id) {
            return true;
        }
    }
    return false;
}

static void remember_delivered(spw_vspw_engine_t* backend,
                               spw_vspw_tp_message_type_t type,
                               uint32_t message_id) {
    size_t index;
    if (message_id == 0u || recently_delivered(backend, type, message_id)) {
        return;
    }
    if (backend->recent_message_count < SPW_VSPW_RECENT_MESSAGE_DEPTH) {
        index = (backend->recent_message_head + backend->recent_message_count) %
                SPW_VSPW_RECENT_MESSAGE_DEPTH;
        backend->recent_messages[index].type = type;
        backend->recent_messages[index].message_id = message_id;
        ++backend->recent_message_count;
        return;
    }
    backend->recent_messages[backend->recent_message_head].type = type;
    backend->recent_messages[backend->recent_message_head].message_id = message_id;
    backend->recent_message_head =
        (backend->recent_message_head + 1u) % SPW_VSPW_RECENT_MESSAGE_DEPTH;
}

static bool reset_remote_session(spw_vspw_engine_t* backend,
                                 uint64_t session_id) {
    if (session_id == 0u || is_retired_session(backend, session_id)) {
        return false;
    }
    if (backend->remote_session_id == session_id) {
        note_peer_activity(backend);
        return true;
    }
    if (backend->remote_session_id != 0u) {
        remember_retired_session(backend, backend->remote_session_id);
    }
    backend->remote_session_id = session_id;
    clear_reassembly(backend);
    clear_recent_messages(backend);
    if (backend->pending_tx_kind != SPW_VSPW_PENDING_NONE) {
        backend->pending_tx_retries = 0u;
        backend->pending_tx_failed = false;
        backend->pending_tx_last_send_us = 0u;
    }
    note_peer_activity(backend);
    return true;
}

static spw_result_t send_datagram_raw(spw_vspw_engine_t* backend,
                                      const uint8_t* bytes,
                                      size_t size,
                                      spw_timeout_us_t timeout_us) {
    return spw_transport_provider_send(&backend->transport,
                                       &backend->remote_peer,
                                       bytes, size, timeout_us);
}

static bool transport_is_up(const spw_vspw_engine_t* backend) {
    spw_transport_state_t state = SPW_TRANSPORT_STATE_DOWN;
    return spw_transport_provider_get_state(&backend->transport, &state) ==
               SPW_OK &&
           state == SPW_TRANSPORT_STATE_UP;
}

static spw_result_t wait_transport_fault_delay(
    spw_vspw_engine_t* backend,
    uint32_t delay_us,
    spw_timeout_us_t timeout_us) {
    return spw_vspw_runtime_delay_us(&backend->runtime, delay_us, timeout_us);
}

static spw_result_t send_datagram(spw_vspw_engine_t* backend,
                                  const uint8_t* bytes,
                                  size_t size,
                                  spw_timeout_us_t timeout_us) {
    spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
    spw_fault_decision_t decision;
    spw_result_t result;

    if (backend->reordered_message_valid) {
        result = send_datagram_raw(backend, bytes, size, timeout_us);
        if (result != SPW_OK) {
            return result;
        }
        result = send_datagram_raw(backend, backend->reordered_message,
                                   backend->reordered_message_size, timeout_us);
        clear_reordered_message(backend);
        return result;
    }

    if (spw_vspw_tp_decode_header(bytes, size, &header) !=
        SPW_VSPW_TP_DECODE_OK) {
        return send_datagram_raw(backend, bytes, size, timeout_us);
    }

    decision = spw_fault_inject_transport(&backend->fault_injector, header.type);
    switch (decision.action) {
    case SPW_UDP_FAULT_ACTION_TRANSPORT_DROP:
        ++backend->fault_statistics.transport_drops;
        ++backend->statistics.dropped_packets;
        return SPW_OK;

    case SPW_UDP_FAULT_ACTION_TRANSPORT_DUPLICATE:
        ++backend->fault_statistics.transport_duplicates;
        result = send_datagram_raw(backend, bytes, size, timeout_us);
        return result == SPW_OK
                   ? send_datagram_raw(backend, bytes, size, timeout_us)
                   : result;

    case SPW_UDP_FAULT_ACTION_TRANSPORT_REORDER:
        ++backend->fault_statistics.transport_reorders;
        if (size > sizeof(backend->reordered_message)) {
            return SPW_ERR_BACKEND;
        }
        memcpy(backend->reordered_message, bytes, size);
        backend->reordered_message_size = size;
        backend->reordered_message_valid = true;
        return SPW_OK;

    case SPW_UDP_FAULT_ACTION_TRANSPORT_DELAY:
        ++backend->fault_statistics.transport_delays;
        result = wait_transport_fault_delay(backend, decision.delay_us, timeout_us);
        return result == SPW_OK
                   ? send_datagram_raw(backend, bytes, size, timeout_us)
                   : result;

    case SPW_UDP_FAULT_ACTION_NONE:
    case SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP:
    default:
        return send_datagram_raw(backend, bytes, size, timeout_us);
    }
}

static spw_result_t pump_one(spw_vspw_engine_t* backend,
                             spw_timeout_us_t timeout_us);
static spw_result_t service_pending_tx(spw_vspw_engine_t* backend);
static spw_result_t send_keepalive(spw_vspw_engine_t* backend,
                                   spw_timeout_us_t timeout_us);

static void maybe_send_keepalive(spw_vspw_engine_t* backend) {
    const uint64_t now = now_us(backend);
    const uint64_t interval =
        (uint64_t)backend->config.keepalive_interval_ms * UINT64_C(1000);
    if (!transport_is_up(backend) || backend->local_session_id == 0u ||
        (backend->state != SPW_LINK_CONNECTING &&
         backend->state != SPW_LINK_RUN &&
         backend->state != SPW_LINK_ERROR_WAIT)) {
        return;
    }
    if (backend->last_keepalive_tx_us == 0u ||
        (now >= backend->last_keepalive_tx_us &&
         now - backend->last_keepalive_tx_us >= interval)) {
        (void)send_keepalive(backend, SPW_TIMEOUT_IMMEDIATE);
    }
}

static spw_result_t wait_virtual_link_delay(spw_vspw_engine_t* backend,
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

    deadline = deadline_make(backend, timeout_us);
    target = now_us(backend) + delay_us;
    while (now_us(backend) < target) {
        const uint64_t now = now_us(backend);
        const uint64_t remaining = target > now ? target - now : 0u;
        const spw_timeout_us_t timing_slice =
            remaining == 0u ? SPW_TIMEOUT_IMMEDIATE : remaining;
        const spw_result_t result = pump_one(
            backend, min_timeout(deadline_remaining(backend, &deadline), timing_slice));
        if (result != SPW_OK && result != SPW_ERR_TIMEOUT &&
            result != SPW_ERR_RESOURCE_EXHAUSTED) {
            return result;
        }
        if (now_us(backend) >= target) {
            return SPW_OK;
        }
        if (deadline_expired(backend, &deadline)) {
            return SPW_ERR_TIMEOUT;
        }
    }
    return SPW_OK;
}

static spw_result_t send_ack(spw_vspw_engine_t* backend,
                             uint32_t message_id) {
    spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
    if (message_id == 0u) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (backend->remote_session_id == 0u) {
        return SPW_ERR_INVALID_STATE;
    }

    header.type = SPW_VSPW_TP_ACK;
    header.payload_size = SPW_VSPW_TP_ACK_PAYLOAD_SIZE;
    header.link_id = backend->config.link_id;
    header.session_id = backend->local_session_id;
    header.sequence = take_nonzero(&backend->next_sequence);
    header.message_id = message_id;
    header.total_size = SPW_VSPW_TP_ACK_PAYLOAD_SIZE;
    if (!spw_vspw_tp_encode_header(&header, backend->control_message,
                                   sizeof(backend->control_message)) ||
        !spw_vspw_tp_encode_ack_payload(
            backend->remote_session_id,
            backend->control_message + SPW_VSPW_TP_HEADER_SIZE,
            SPW_VSPW_TP_ACK_PAYLOAD_SIZE)) {
        return SPW_ERR_BACKEND;
    }
    return send_datagram(backend, backend->control_message,
                         SPW_VSPW_TP_HEADER_SIZE +
                             SPW_VSPW_TP_ACK_PAYLOAD_SIZE,
                         SPW_TIMEOUT_IMMEDIATE);
}

static spw_result_t send_keepalive(spw_vspw_engine_t* backend,
                                   spw_timeout_us_t timeout_us) {
    spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
    spw_result_t result;
    header.type = SPW_VSPW_TP_KEEPALIVE;
    header.link_id = backend->config.link_id;
    header.session_id = backend->local_session_id;
    header.sequence = take_nonzero(&backend->next_sequence);
    if (!spw_vspw_tp_encode_header(&header, backend->control_message,
                                   sizeof(backend->control_message))) {
        return SPW_ERR_BACKEND;
    }
    result = send_datagram(backend, backend->control_message,
                           SPW_VSPW_TP_HEADER_SIZE, timeout_us);
    if (result == SPW_OK) {
        backend->last_keepalive_tx_us = now_us(backend);
    }
    return result;
}

static spw_result_t transmit_pending(spw_vspw_engine_t* backend,
                                     spw_timeout_us_t timeout_us) {
    size_t offset;
    size_t fragment_size;
    bool fragmented;

    if (backend->pending_tx_kind == SPW_VSPW_PENDING_NONE ||
        backend->pending_tx_message_id == 0u) {
        return SPW_OK;
    }

    if (backend->pending_tx_kind == SPW_VSPW_PENDING_TIME_CODE) {
        spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
        spw_result_t result;
        header.type = SPW_VSPW_TP_TIME_CODE;
        header.flags = SPW_VSPW_TP_FLAG_ACK_REQUIRED;
        header.payload_size = SPW_VSPW_TP_TIME_CODE_PAYLOAD_SIZE;
        header.link_id = backend->config.link_id;
        header.session_id = backend->local_session_id;
        header.sequence = take_nonzero(&backend->next_sequence);
        header.message_id = backend->pending_tx_message_id;
        header.total_size = SPW_VSPW_TP_TIME_CODE_PAYLOAD_SIZE;
        if (!spw_vspw_tp_encode_header(&header, backend->tx_message,
                                       sizeof(backend->tx_message))) {
            return SPW_ERR_BACKEND;
        }
        backend->tx_message[SPW_VSPW_TP_HEADER_SIZE] =
            backend->pending_tx_time_code.time_count;
        backend->tx_message[SPW_VSPW_TP_HEADER_SIZE + 1u] =
            backend->pending_tx_time_code.control_flags;
        result = send_datagram(
            backend, backend->tx_message,
            SPW_VSPW_TP_HEADER_SIZE + SPW_VSPW_TP_TIME_CODE_PAYLOAD_SIZE,
            timeout_us);
        if (result == SPW_OK) {
            backend->pending_tx_last_send_us = now_us(backend);
        }
        return result;
    }

    fragment_size = backend->config.fragment_payload_size;
    fragmented = backend->pending_tx_packet_size > fragment_size;
    offset = 0u;
    do {
        const size_t remaining = backend->pending_tx_packet_size - offset;
        const size_t payload_size =
            fragmented
                ? (fragment_size < remaining ? fragment_size : remaining)
                : remaining;
        spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
        spw_result_t result;

        header.type = SPW_VSPW_TP_DATA;
        header.flags = terminator_flag(backend->pending_tx_terminator) |
                       SPW_VSPW_TP_FLAG_ACK_REQUIRED;
        if (fragmented && offset == 0u) {
            header.flags |= SPW_VSPW_TP_FLAG_FRAGMENT_START;
        }
        if (fragmented && offset + payload_size ==
                              backend->pending_tx_packet_size) {
            header.flags |= SPW_VSPW_TP_FLAG_FRAGMENT_END;
        }
        header.payload_size = (uint16_t)payload_size;
        header.link_id = backend->config.link_id;
        header.session_id = backend->local_session_id;
        header.sequence = take_nonzero(&backend->next_sequence);
        header.message_id = backend->pending_tx_message_id;
        header.fragment_offset = (uint32_t)offset;
        header.total_size = (uint32_t)backend->pending_tx_packet_size;

        if (!spw_vspw_tp_encode_header(&header, backend->tx_message,
                                       sizeof(backend->tx_message))) {
            return SPW_ERR_INVALID_PACKET;
        }
        if (payload_size != 0u) {
            memcpy(backend->tx_message + SPW_VSPW_TP_HEADER_SIZE,
                   backend->pending_tx_packet + offset, payload_size);
        }
        result = send_datagram(
            backend, backend->tx_message,
            SPW_VSPW_TP_HEADER_SIZE + payload_size, timeout_us);
        if (result != SPW_OK) {
            return result;
        }
        offset += payload_size;
    } while (offset < backend->pending_tx_packet_size);

    backend->pending_tx_last_send_us = now_us(backend);
    return SPW_OK;
}

static spw_result_t service_pending_tx(spw_vspw_engine_t* backend) {
    const uint64_t now = now_us(backend);
    const uint64_t ack_timeout =
        (uint64_t)backend->config.ack_timeout_ms * UINT64_C(1000);
    spw_result_t result;

    if (backend->pending_tx_kind == SPW_VSPW_PENDING_NONE) {
        return SPW_OK;
    }
    if (backend->pending_tx_last_send_us != 0u &&
        now >= backend->pending_tx_last_send_us &&
        now - backend->pending_tx_last_send_us < ack_timeout) {
        return SPW_OK;
    }
    if (backend->pending_tx_retries >= backend->config.max_retries) {
        if (!backend->pending_tx_failed) {
            backend->pending_tx_failed = true;
            ++backend->statistics.dropped_packets;
        }
        mark_peer_lost(backend);
        return SPW_ERR_LINK_UNAVAILABLE;
    }

    result = transmit_pending(backend, SPW_TIMEOUT_IMMEDIATE);
    if (result == SPW_OK) {
        ++backend->pending_tx_retries;
    }
    return result;
}

static spw_result_t process_ack(spw_vspw_engine_t* backend,
                                const spw_vspw_tp_header_t* header,
                                const uint8_t* payload) {
    uint64_t acknowledged_session_id = 0u;
    if (!spw_vspw_tp_decode_ack_payload(payload, header->payload_size,
                                        &acknowledged_session_id)) {
        ++backend->statistics.dropped_packets;
        return SPW_OK;
    }
    if (acknowledged_session_id != backend->local_session_id) {
        return SPW_OK;
    }
    if (backend->pending_tx_kind != SPW_VSPW_PENDING_NONE &&
        header->message_id == backend->pending_tx_message_id) {
        clear_pending_tx(backend);
    }
    return SPW_OK;
}

static spw_result_t process_keepalive(spw_vspw_engine_t* backend,
                                      const spw_vspw_tp_header_t* header) {
    (void)reset_remote_session(backend, header->session_id);
    return SPW_OK;
}

static spw_result_t process_time_code(spw_vspw_engine_t* backend,
                                      const spw_vspw_tp_header_t* header,
                                      const uint8_t* payload) {
    const bool ack_required =
        (header->flags & SPW_VSPW_TP_FLAG_ACK_REQUIRED) != 0u;
    spw_time_code_t time_code;
    size_t index;

    if (ack_required && recently_delivered(
                            backend, SPW_VSPW_TP_TIME_CODE,
                            header->message_id)) {
        (void)send_ack(backend, header->message_id);
        return SPW_OK;
    }
    if (backend->time_code_count == SPW_VSPW_TIME_CODE_QUEUE_DEPTH) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    time_code.time_count = payload[0];
    time_code.control_flags = payload[1];
    if (!valid_time_code(&time_code)) {
        ++backend->statistics.dropped_packets;
        return SPW_OK;
    }

    index = (backend->time_code_head + backend->time_code_count) %
            SPW_VSPW_TIME_CODE_QUEUE_DEPTH;
    backend->time_codes[index] = time_code;
    ++backend->time_code_count;
    if (ack_required) {
        remember_delivered(backend, SPW_VSPW_TP_TIME_CODE, header->message_id);
        (void)send_ack(backend, header->message_id);
    }
    return SPW_OK;
}

static spw_result_t process_data(spw_vspw_engine_t* backend,
                                 const spw_vspw_tp_header_t* header,
                                 const uint8_t* payload) {
    const bool ack_required =
        (header->flags & SPW_VSPW_TP_FLAG_ACK_REQUIRED) != 0u;
    const bool fragmented = header->total_size != header->payload_size;
    const spw_terminator_t terminator =
        (header->flags & SPW_VSPW_TP_FLAG_EEP) != 0u
            ? SPW_TERMINATOR_EEP
            : SPW_TERMINATOR_EOP;

    if (ack_required && recently_delivered(
                            backend, SPW_VSPW_TP_DATA,
                            header->message_id)) {
        (void)send_ack(backend, header->message_id);
        return SPW_OK;
    }

    if (!fragmented) {
        if (backend->pending_packet_valid) {
            return SPW_ERR_RESOURCE_EXHAUSTED;
        }
        if (header->payload_size != 0u) {
            memcpy(backend->pending_packet, payload, header->payload_size);
        }
        backend->pending_packet_size = header->payload_size;
        backend->pending_packet_terminator = terminator;
        backend->pending_packet_valid = true;
        SPW_PROFILE_RX_PROVIDER_BOUNDARY();
        if (ack_required) {
            remember_delivered(backend, SPW_VSPW_TP_DATA, header->message_id);
            (void)send_ack(backend, header->message_id);
        }
        return SPW_OK;
    }

    expire_reassembly(backend);
    {
        const spw_reassembly_result_t result =
            spw_fragment_reassembler_push(&backend->reassembly, header, payload);
        if (result == SPW_REASSEMBLY_INVALID ||
            result == SPW_REASSEMBLY_CONFLICT) {
            ++backend->statistics.dropped_packets;
            return SPW_OK;
        }
        backend->reassembly_last_fragment_us = now_us(backend);
        if (result != SPW_REASSEMBLY_COMPLETE) {
            return SPW_OK;
        }
    }

    if (backend->pending_packet_valid) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    if (backend->reassembly.total_size != 0u) {
        memcpy(backend->pending_packet, backend->reassembly.data,
               backend->reassembly.total_size);
    }
    backend->pending_packet_size = backend->reassembly.total_size;
    backend->pending_packet_terminator =
        (backend->reassembly.terminator_flags & SPW_VSPW_TP_FLAG_EEP) != 0u
            ? SPW_TERMINATOR_EEP
            : SPW_TERMINATOR_EOP;
    backend->pending_packet_valid = true;
    SPW_PROFILE_RX_PROVIDER_BOUNDARY();
    {
        const uint32_t completed_message_id = backend->reassembly.message_id;
        const bool completed_ack_required = backend->reassembly.ack_required;
        clear_reassembly(backend);
        if (completed_ack_required) {
            remember_delivered(backend, SPW_VSPW_TP_DATA,
                               completed_message_id);
            (void)send_ack(backend, completed_message_id);
        }
    }
    return SPW_OK;
}

static spw_result_t pump_one(spw_vspw_engine_t* backend,
                             spw_timeout_us_t timeout_us) {
    spw_timeout_us_t keepalive_slice;
    spw_timeout_us_t service_slice;
    spw_timeout_us_t wait_timeout;
    spw_result_t receive_result;
    size_t received = 0u;
    spw_transport_peer_id_t source = SPW_TRANSPORT_PEER_ID_INITIALIZER;
    spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
    const uint8_t* payload;

    maybe_send_keepalive(backend);
    keepalive_slice =
        (spw_timeout_us_t)backend->config.keepalive_interval_ms * 1000u;
    service_slice = keepalive_slice;
    if (backend->pending_tx_kind != SPW_VSPW_PENDING_NONE) {
        const spw_timeout_us_t ack_slice =
            (spw_timeout_us_t)backend->config.ack_timeout_ms * 1000u;
        service_slice = min_timeout(service_slice, ack_slice);
    }
    wait_timeout = min_timeout(timeout_us, service_slice);

    receive_result = spw_transport_provider_receive(
        &backend->transport, backend->rx_message,
        sizeof(backend->rx_message), &received, &source, wait_timeout);

    if (receive_result == SPW_ERR_TIMEOUT) {
        maybe_send_keepalive(backend);
        refresh_peer_state(backend);
        if (timeout_us == SPW_TIMEOUT_INFINITE || wait_timeout < timeout_us) {
            return SPW_OK;
        }
        return SPW_ERR_TIMEOUT;
    }
    if (receive_result != SPW_OK) {
        return receive_result;
    }
    if (!spw_transport_peer_id_equal(&source, &backend->remote_peer)) {
        return SPW_OK;
    }

    if (spw_vspw_tp_decode_header(backend->rx_message, received, &header) !=
            SPW_VSPW_TP_DECODE_OK ||
        received != SPW_VSPW_TP_HEADER_SIZE + header.payload_size) {
        ++backend->statistics.dropped_packets;
        return SPW_OK;
    }
    if (header.link_id != backend->config.link_id) {
        return SPW_OK;
    }

    payload = backend->rx_message + SPW_VSPW_TP_HEADER_SIZE;
    if (header.type == SPW_VSPW_TP_KEEPALIVE) {
        return process_keepalive(backend, &header);
    }
    if (backend->remote_session_id == 0u ||
        header.session_id != backend->remote_session_id) {
        return SPW_OK;
    }
    note_peer_activity(backend);

    switch (header.type) {
    case SPW_VSPW_TP_DATA:
        if (header.total_size > SPW_VSPW_ENGINE_MAX_PACKET_SIZE) {
            ++backend->statistics.dropped_packets;
            return SPW_OK;
        }
        return process_data(backend, &header, payload);
    case SPW_VSPW_TP_TIME_CODE:
        return process_time_code(backend, &header, payload);
    case SPW_VSPW_TP_ACK:
        return process_ack(backend, &header, payload);
    case SPW_VSPW_TP_KEEPALIVE:
    case SPW_VSPW_TP_LINK_CONTROL:
    default:
        return SPW_OK;
    }
}

static spw_result_t ensure_peer(spw_vspw_engine_t* backend,
                                spw_timeout_us_t timeout_us) {
    spw_vspw_deadline_t deadline;
    refresh_peer_state(backend);
    if (backend->state == SPW_LINK_RUN && peer_is_current(backend)) {
        return SPW_OK;
    }
    if (backend->state != SPW_LINK_CONNECTING &&
        backend->state != SPW_LINK_ERROR_WAIT &&
        backend->state != SPW_LINK_RUN) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }

    deadline = deadline_make(backend, timeout_us);
    for (;;) {
        spw_result_t result;
        maybe_send_keepalive(backend);
        result = pump_one(backend, deadline_remaining(backend, &deadline));
        refresh_peer_state(backend);
        if (backend->state == SPW_LINK_RUN && peer_is_current(backend)) {
            return SPW_OK;
        }
        if (result != SPW_OK && result != SPW_ERR_TIMEOUT &&
            result != SPW_ERR_RESOURCE_EXHAUSTED) {
            return result;
        }
        if (deadline_expired(backend, &deadline) ||
            timeout_us == SPW_TIMEOUT_IMMEDIATE) {
            return SPW_ERR_LINK_UNAVAILABLE;
        }
    }
}

static spw_result_t wait_for_tx_slot(spw_vspw_engine_t* backend,
                                     spw_timeout_us_t timeout_us) {
    spw_vspw_deadline_t deadline;
    if (backend->pending_tx_kind == SPW_VSPW_PENDING_NONE) {
        return SPW_OK;
    }

    deadline = deadline_make(backend, timeout_us);
    for (;;) {
        spw_result_t service_result = service_pending_tx(backend);
        spw_result_t pump_result;
        spw_timeout_us_t retry_slice;
        if (backend->pending_tx_kind == SPW_VSPW_PENDING_NONE) {
            return SPW_OK;
        }
        if (service_result == SPW_ERR_LINK_UNAVAILABLE) {
            return service_result;
        }
        if (service_result != SPW_OK && service_result != SPW_ERR_TIMEOUT) {
            return service_result;
        }

        retry_slice =
            (spw_timeout_us_t)backend->config.ack_timeout_ms * 1000u;
        pump_result = pump_one(
            backend, min_timeout(deadline_remaining(backend, &deadline), retry_slice));
        if (pump_result != SPW_OK && pump_result != SPW_ERR_TIMEOUT &&
            pump_result != SPW_ERR_RESOURCE_EXHAUSTED) {
            return pump_result;
        }
        if (backend->pending_tx_kind == SPW_VSPW_PENDING_NONE) {
            return SPW_OK;
        }
        if (deadline_expired(backend, &deadline) ||
            timeout_us == SPW_TIMEOUT_IMMEDIATE) {
            return SPW_ERR_TIMEOUT;
        }
    }
}

spw_result_t spw_vspw_engine_init(
    spw_vspw_engine_t* backend,
    const spw_vspw_engine_config_t* config,
    const spw_transport_provider_t* transport,
    const spw_transport_peer_id_t* remote_peer,
    const spw_vspw_runtime_t* runtime,
    const spw_deterministic_fault_injector_t* fault_template) {
    if (backend == NULL || config == NULL || transport == NULL ||
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

    memset(backend, 0, sizeof(*backend));
    backend->config = *config;
    backend->transport = *transport;
    backend->remote_peer = *remote_peer;
    backend->runtime = *runtime;
    if (fault_template != NULL) {
        backend->fault_injector = *fault_template;
    }
    backend->virtual_timing.link_bps = config->virtual_link_bps;
    backend->virtual_timing.latency_us = config->virtual_latency_us;
    backend->state = SPW_LINK_READY;
    backend->next_sequence = 1u;
    backend->next_message_id = 1u;
    backend->pending_packet_terminator = SPW_TERMINATOR_EOP;
    backend->pending_tx_terminator = SPW_TERMINATOR_EOP;
    spw_fragment_reassembler_init(
        &backend->reassembly, backend->reassembly_data,
        sizeof(backend->reassembly_data), backend->reassembly_coverage,
        sizeof(backend->reassembly_coverage) /
            sizeof(backend->reassembly_coverage[0]));
    return SPW_OK;
}

spw_result_t spw_vspw_engine_start(spw_vspw_engine_t* backend) {
    spw_result_t result = spw_transport_provider_start(&backend->transport);
    if (result != SPW_OK) {
        return result;
    }

    clear_reassembly(backend);
    clear_pending_tx(backend);
    clear_recent_messages(backend);
    clear_retired_sessions(backend);
    clear_reordered_message(backend);
    spw_fault_injector_reset(&backend->fault_injector);
    backend->pending_packet_valid = false;
    backend->time_code_head = 0u;
    backend->time_code_count = 0u;
    backend->next_sequence = 1u;
    backend->next_message_id = 1u;
    backend->local_session_id = make_session_id(backend);
    backend->remote_session_id = 0u;
    backend->peer_seen = false;
    backend->last_peer_rx_us = 0u;
    backend->last_keepalive_tx_us = 0u;
    backend->state = SPW_LINK_CONNECTING;

    result = send_keepalive(backend, SPW_TIMEOUT_IMMEDIATE);
    return result == SPW_ERR_TIMEOUT ? SPW_OK : result;
}

spw_result_t spw_vspw_engine_stop(spw_vspw_engine_t* backend) {

    backend->state = SPW_LINK_READY;
    clear_reassembly(backend);
    clear_pending_tx(backend);
    clear_recent_messages(backend);
    clear_retired_sessions(backend);
    clear_reordered_message(backend);
    backend->pending_packet_valid = false;
    backend->time_code_head = 0u;
    backend->time_code_count = 0u;
    backend->peer_seen = false;
    backend->remote_session_id = 0u;

    return spw_transport_provider_stop(&backend->transport);
}

spw_result_t spw_vspw_engine_reset(spw_vspw_engine_t* backend) {

    spw_fault_injector_reset(&backend->fault_injector);
    backend->state = SPW_LINK_ERROR_RESET;
    clear_reassembly(backend);
    clear_pending_tx(backend);
    clear_recent_messages(backend);
    clear_retired_sessions(backend);
    clear_reordered_message(backend);
    backend->pending_packet_valid = false;
    backend->time_code_head = 0u;
    backend->time_code_count = 0u;
    backend->peer_seen = false;
    backend->remote_session_id = 0u;

    return spw_transport_provider_reset(&backend->transport);
}

spw_result_t spw_vspw_engine_get_link_state(
    spw_vspw_engine_t* backend,
    spw_link_state_t* out_state) {
    if (backend->state == SPW_LINK_CONNECTING ||
        backend->state == SPW_LINK_RUN ||
        backend->state == SPW_LINK_ERROR_WAIT) {
        maybe_send_keepalive(backend);
        (void)pump_one(backend, SPW_TIMEOUT_IMMEDIATE);
        (void)service_pending_tx(backend);
        refresh_peer_state(backend);
    }
    *out_state = backend->state;
    return SPW_OK;
}

static spw_result_t spw_vspw_engine_get_capabilities(const spw_vspw_engine_t* backend,
                                         spw_capabilities_t* out_capabilities) {
    (void)context;
    out_capabilities->bits = SPW_CAP_EEP | SPW_CAP_TIME_CODE |
                             SPW_CAP_STATISTICS | SPW_CAP_RATE_CONTROL |
                             SPW_CAP_FAULT_INJECTION;
    out_capabilities->max_packet_size = SPW_VSPW_ENGINE_MAX_PACKET_SIZE;
    out_capabilities->tx_queue_depth = 1u;
    out_capabilities->rx_queue_depth = 1u;
    out_capabilities->buffer_alignment = alignof(spw_vspw_max_alignment_t);
    return SPW_OK;
}

bool spw_vspw_engine_supports_zero_copy(const spw_vspw_engine_t* backend) {
    (void)backend;
    return false;
}

spw_result_t spw_vspw_engine_send(
    spw_vspw_engine_t* backend,
    const spw_packet_t* packet,
    spw_timeout_us_t timeout_us) {
    spw_vspw_deadline_t deadline;
    spw_result_t result;
    spw_terminator_t effective_terminator;

    SPW_PROFILE_TX_BACKEND_ENTRY();
    if ((packet->length != 0u && packet->data == NULL) ||
        packet->length > SPW_VSPW_ENGINE_MAX_PACKET_SIZE ||
        !valid_terminator(packet->terminator)) {
        return SPW_ERR_INVALID_PACKET;
    }

    deadline = deadline_make(backend, timeout_us);
    result = ensure_peer(backend, deadline_remaining(backend, &deadline));
    if (result != SPW_OK) {
        return result;
    }
    result = wait_for_tx_slot(backend, deadline_remaining(backend, &deadline));
    if (result != SPW_OK) {
        return result;
    }

    result = wait_virtual_link_delay(
        backend,
        spw_virtual_link_delay_us(&backend->virtual_timing,
                                  SPW_VIRTUAL_LINK_EVENT_DATA,
                                  packet->length),
        deadline_remaining(backend, &deadline));
    if (result != SPW_OK) {
        return result;
    }

    effective_terminator = packet->terminator;
    if (packet->terminator == SPW_TERMINATOR_EOP &&
        spw_fault_inject_spacewire_eep(&backend->fault_injector)) {
        effective_terminator = SPW_TERMINATOR_EEP;
        ++backend->fault_statistics.spacewire_eep_injections;
    }

    if (packet->length != 0u) {
        memcpy(backend->pending_tx_packet, packet->data, packet->length);
    }
    backend->pending_tx_packet_size = packet->length;
    backend->pending_tx_terminator = effective_terminator;
    backend->pending_tx_kind = SPW_VSPW_PENDING_DATA;
    backend->pending_tx_message_id = take_nonzero(&backend->next_message_id);
    backend->pending_tx_retries = 0u;
    backend->pending_tx_last_send_us = 0u;

    (void)send_keepalive(backend, SPW_TIMEOUT_IMMEDIATE);
    SPW_PROFILE_TX_PROVIDER_ENTRY();
    result = transmit_pending(backend, deadline_remaining(backend, &deadline));
    if (result != SPW_OK) {
        clear_pending_tx(backend);
        return result;
    }
    /* All fragments of this logical packet have been handed to UDP. */
    SPW_PROFILE_TX_PROVIDER_BOUNDARY();

    ++backend->statistics.tx_packets;
    backend->statistics.tx_bytes += packet->length;
    if (backend->pending_tx_terminator == SPW_TERMINATOR_EEP) {
        ++backend->statistics.eep_packets;
    }
    return SPW_OK;
}

spw_result_t spw_vspw_engine_receive(
    spw_vspw_engine_t* backend,
    spw_packet_t* packet,
    spw_timeout_us_t timeout_us) {
    spw_vspw_deadline_t deadline = deadline_make(backend, timeout_us);
    spw_result_t peer_result = ensure_peer(
        backend, deadline_remaining(backend, &deadline));
    if (peer_result != SPW_OK && !backend->pending_packet_valid) {
        return peer_result;
    }

    while (!backend->pending_packet_valid) {
        spw_result_t service_result = service_pending_tx(backend);
        spw_result_t result;
        if (service_result == SPW_ERR_LINK_UNAVAILABLE) {
            return service_result;
        }
        result = pump_one(backend, deadline_remaining(backend, &deadline));
        if (result != SPW_OK && result != SPW_ERR_RESOURCE_EXHAUSTED &&
            result != SPW_ERR_TIMEOUT) {
            return result;
        }
        if (!backend->pending_packet_valid &&
            (deadline_expired(backend, &deadline) ||
             timeout_us == SPW_TIMEOUT_IMMEDIATE)) {
            refresh_peer_state(backend);
            return backend->state == SPW_LINK_ERROR_WAIT
                       ? SPW_ERR_LINK_UNAVAILABLE
                       : SPW_ERR_TIMEOUT;
        }
    }

    packet->length = backend->pending_packet_size;
    packet->terminator = backend->pending_packet_terminator;
    if (packet->capacity < backend->pending_packet_size ||
        (backend->pending_packet_size != 0u && packet->data == NULL)) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }
    if (backend->pending_packet_size != 0u) {
        memcpy(packet->data, backend->pending_packet,
               backend->pending_packet_size);
    }
    backend->pending_packet_valid = false;
    SPW_PROFILE_RX_PROVIDER_RETURN();
    ++backend->statistics.rx_packets;
    backend->statistics.rx_bytes += packet->length;
    SPW_PROFILE_RX_BACKEND_RETURN();
    return SPW_OK;
}

spw_result_t spw_vspw_engine_send_time_code(
    spw_vspw_engine_t* backend,
    const spw_time_code_t* time_code,
    spw_timeout_us_t timeout_us) {
    spw_vspw_deadline_t deadline;
    spw_result_t result;
    if (!valid_time_code(time_code)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    deadline = deadline_make(backend, timeout_us);
    result = ensure_peer(backend, deadline_remaining(backend, &deadline));
    if (result != SPW_OK) {
        return result;
    }
    result = wait_for_tx_slot(backend, deadline_remaining(backend, &deadline));
    if (result != SPW_OK) {
        return result;
    }

    result = wait_virtual_link_delay(
        backend,
        spw_virtual_link_delay_us(&backend->virtual_timing,
                                  SPW_VIRTUAL_LINK_EVENT_TIME_CODE,
                                  SPW_VSPW_TP_TIME_CODE_PAYLOAD_SIZE),
        deadline_remaining(backend, &deadline));
    if (result != SPW_OK) {
        return result;
    }

    backend->pending_tx_time_code = *time_code;
    backend->pending_tx_kind = SPW_VSPW_PENDING_TIME_CODE;
    backend->pending_tx_message_id = take_nonzero(&backend->next_message_id);
    backend->pending_tx_retries = 0u;
    backend->pending_tx_last_send_us = 0u;

    (void)send_keepalive(backend, SPW_TIMEOUT_IMMEDIATE);
    result = transmit_pending(backend, deadline_remaining(backend, &deadline));
    if (result != SPW_OK) {
        clear_pending_tx(backend);
        return result;
    }
    ++backend->statistics.tx_time_codes;
    return SPW_OK;
}

spw_result_t spw_vspw_engine_receive_time_code(
    spw_vspw_engine_t* backend,
    spw_time_code_t* time_code,
    spw_timeout_us_t timeout_us) {
    spw_vspw_deadline_t deadline = deadline_make(backend, timeout_us);
    spw_result_t peer_result = ensure_peer(
        backend, deadline_remaining(backend, &deadline));
    if (peer_result != SPW_OK && backend->time_code_count == 0u) {
        return peer_result;
    }

    while (backend->time_code_count == 0u) {
        spw_result_t service_result = service_pending_tx(backend);
        spw_result_t result;
        if (service_result == SPW_ERR_LINK_UNAVAILABLE) {
            return service_result;
        }
        result = pump_one(backend, deadline_remaining(backend, &deadline));
        if (result != SPW_OK && result != SPW_ERR_RESOURCE_EXHAUSTED &&
            result != SPW_ERR_TIMEOUT) {
            return result;
        }
        if (backend->time_code_count == 0u &&
            (deadline_expired(backend, &deadline) ||
             timeout_us == SPW_TIMEOUT_IMMEDIATE)) {
            refresh_peer_state(backend);
            return backend->state == SPW_LINK_ERROR_WAIT
                       ? SPW_ERR_LINK_UNAVAILABLE
                       : SPW_ERR_TIMEOUT;
        }
    }

    *time_code = backend->time_codes[backend->time_code_head];
    backend->time_code_head =
        (backend->time_code_head + 1u) % SPW_VSPW_TIME_CODE_QUEUE_DEPTH;
    --backend->time_code_count;
    ++backend->statistics.rx_time_codes;
    return SPW_OK;
}

spw_result_t spw_vspw_engine_get_statistics(
    const spw_vspw_engine_t* backend,
    spw_statistics_t* out_statistics) {
    *out_statistics = backend->statistics;
    return SPW_OK;
}

spw_result_t spw_vspw_engine_clear_statistics(spw_vspw_engine_t* backend) {
    memset(&backend->statistics, 0, sizeof(backend->statistics));
    return SPW_OK;
}

spw_result_t spw_vspw_engine_get_fault_statistics(
    const spw_vspw_engine_t* backend,
    spw_fault_statistics_t* out_statistics) {
    *out_statistics = backend->fault_statistics;
    return SPW_OK;
}

spw_result_t spw_vspw_engine_clear_fault_statistics(
    spw_vspw_engine_t* backend) {
    memset(&backend->fault_statistics, 0, sizeof(backend->fault_statistics));
    return SPW_OK;
}

