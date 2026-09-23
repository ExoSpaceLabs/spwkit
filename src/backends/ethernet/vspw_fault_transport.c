// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/vspw_fault_transport.h"

#include <string.h>

static void clear_reordered(spw_vspw_fault_transport_t* transport) {
    transport->reordered_size = 0u;
    transport->reordered_peer =
        (spw_transport_peer_id_t)SPW_TRANSPORT_PEER_ID_INITIALIZER;
    transport->reordered_valid = false;
}

static spw_result_t fault_start(void* context) {
    spw_vspw_fault_transport_t* transport =
        (spw_vspw_fault_transport_t*)context;
    clear_reordered(transport);
    return spw_transport_provider_start(&transport->underlying);
}

static spw_result_t fault_stop(void* context) {
    spw_vspw_fault_transport_t* transport =
        (spw_vspw_fault_transport_t*)context;
    clear_reordered(transport);
    return spw_transport_provider_stop(&transport->underlying);
}

static spw_result_t fault_reset(void* context) {
    spw_vspw_fault_transport_t* transport =
        (spw_vspw_fault_transport_t*)context;
    clear_reordered(transport);
    return spw_transport_provider_reset(&transport->underlying);
}

static spw_result_t send_underlying(
    spw_vspw_fault_transport_t* transport,
    const spw_transport_peer_id_t* peer,
    const uint8_t* message,
    size_t message_size,
    spw_timeout_us_t timeout_us) {
    return spw_transport_provider_send(&transport->underlying, peer, message,
                                       message_size, timeout_us);
}

static spw_result_t fault_send(void* context,
                               const spw_transport_peer_id_t* peer,
                               const uint8_t* message,
                               size_t message_size,
                               spw_timeout_us_t timeout_us) {
    spw_vspw_fault_transport_t* transport =
        (spw_vspw_fault_transport_t*)context;
    spw_vspw_tp_header_t header = SPW_VSPW_TP_HEADER_INITIALIZER;
    spw_fault_decision_t decision;
    spw_result_t result;

    if (transport->reordered_valid) {
        result = send_underlying(transport, peer, message, message_size,
                                 timeout_us);
        if (result != SPW_OK) {
            return result;
        }
        result = send_underlying(transport, &transport->reordered_peer,
                                 transport->reordered_frame,
                                 transport->reordered_size, timeout_us);
        clear_reordered(transport);
        return result;
    }

    if (spw_vspw_tp_decode_header(message, message_size, &header) !=
        SPW_VSPW_TP_DECODE_OK) {
        return send_underlying(transport, peer, message, message_size,
                               timeout_us);
    }

    decision = spw_fault_inject_transport(transport->injector, header.type);
    switch (decision.action) {
    case SPW_UDP_FAULT_ACTION_TRANSPORT_DROP:
        ++transport->fault_statistics->transport_drops;
        ++transport->statistics->dropped_packets;
        return SPW_OK;

    case SPW_UDP_FAULT_ACTION_TRANSPORT_DUPLICATE:
        ++transport->fault_statistics->transport_duplicates;
        result = send_underlying(transport, peer, message, message_size,
                                 timeout_us);
        return result == SPW_OK
                   ? send_underlying(transport, peer, message, message_size,
                                     timeout_us)
                   : result;

    case SPW_UDP_FAULT_ACTION_TRANSPORT_REORDER:
        ++transport->fault_statistics->transport_reorders;
        if (message_size > sizeof(transport->reordered_frame)) {
            return SPW_ERR_BUFFER_TOO_SMALL;
        }
        if (message_size != 0u) {
            memcpy(transport->reordered_frame, message, message_size);
        }
        transport->reordered_size = message_size;
        transport->reordered_peer = *peer;
        transport->reordered_valid = true;
        return SPW_OK;

    case SPW_UDP_FAULT_ACTION_TRANSPORT_DELAY:
        ++transport->fault_statistics->transport_delays;
        result = spw_vspw_runtime_delay_us(&transport->runtime,
                                           decision.delay_us, timeout_us);
        return result == SPW_OK
                   ? send_underlying(transport, peer, message, message_size,
                                     timeout_us)
                   : result;

    case SPW_UDP_FAULT_ACTION_NONE:
    case SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP:
    default:
        return send_underlying(transport, peer, message, message_size,
                               timeout_us);
    }
}

static spw_result_t fault_receive(
    void* context,
    uint8_t* message,
    size_t message_capacity,
    size_t* out_message_size,
    spw_transport_peer_id_t* out_peer,
    spw_timeout_us_t timeout_us) {
    spw_vspw_fault_transport_t* transport =
        (spw_vspw_fault_transport_t*)context;
    return spw_transport_provider_receive(
        &transport->underlying, message, message_capacity, out_message_size,
        out_peer, timeout_us);
}

static spw_result_t fault_wait(void* context,
                               spw_transport_ready_t interests,
                               spw_timeout_us_t timeout_us,
                               spw_transport_ready_t* out_ready) {
    spw_vspw_fault_transport_t* transport =
        (spw_vspw_fault_transport_t*)context;
    return spw_transport_provider_wait(&transport->underlying, interests,
                                       timeout_us, out_ready);
}

static spw_result_t fault_get_mtu(const void* context, size_t* out_mtu) {
    const spw_vspw_fault_transport_t* transport =
        (const spw_vspw_fault_transport_t*)context;
    return spw_transport_provider_get_mtu(&transport->underlying, out_mtu);
}

static spw_result_t fault_get_state(
    const void* context,
    spw_transport_state_t* out_state) {
    const spw_vspw_fault_transport_t* transport =
        (const spw_vspw_fault_transport_t*)context;
    return spw_transport_provider_get_state(&transport->underlying, out_state);
}

static const spw_transport_provider_ops_t FAULT_OPS = {
    fault_start,
    fault_stop,
    fault_reset,
    fault_send,
    fault_receive,
    fault_wait,
    fault_get_mtu,
    fault_get_state
};

spw_result_t spw_vspw_fault_transport_init(
    spw_vspw_fault_transport_t* transport,
    const spw_transport_provider_t* underlying,
    const spw_vspw_runtime_t* runtime,
    spw_deterministic_fault_injector_t* injector,
    spw_statistics_t* statistics,
    spw_fault_statistics_t* fault_statistics) {
    size_t mtu = 0u;
    if (transport == NULL || underlying == NULL || runtime == NULL ||
        injector == NULL || statistics == NULL || fault_statistics == NULL ||
        !spw_transport_provider_valid(underlying) ||
        !spw_vspw_runtime_valid(runtime)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (spw_transport_provider_get_mtu(underlying, &mtu) != SPW_OK ||
        mtu > sizeof(transport->reordered_frame)) {
        return SPW_ERR_UNSUPPORTED;
    }

    memset(transport, 0, sizeof(*transport));
    transport->underlying = *underlying;
    transport->runtime = *runtime;
    transport->injector = injector;
    transport->statistics = statistics;
    transport->fault_statistics = fault_statistics;
    clear_reordered(transport);
    return SPW_OK;
}

void spw_vspw_fault_transport_provider(
    spw_vspw_fault_transport_t* transport,
    spw_transport_provider_t* out_provider) {
    if (out_provider == NULL) {
        return;
    }
    out_provider->ops = transport == NULL ? NULL : &FAULT_OPS;
    out_provider->context = transport;
}
