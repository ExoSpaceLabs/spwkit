// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/raw_ethernet_backend.h"
#include "backends/ethernet/raw_ethernet_transport_provider.h"
#include "backends/ethernet/vspw_engine.h"

#include <spwkit/raw_ethernet.h>

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct spw_raw_runtime_context {
    const spw_runtime_ops_t* ops;
    void* context;
} spw_raw_runtime_context_t;

typedef struct spw_raw_ethernet_backend {
    spw_raw_ethernet_config_t config;
    spw_raw_runtime_context_t runtime_context;
    spw_vspw_runtime_t runtime;
    spw_raw_ethernet_transport_t transport_context;
    spw_transport_provider_t transport;
    spw_transport_peer_id_t remote_peer;
    spw_vspw_engine_t engine;
} spw_raw_ethernet_backend_t;

static uint64_t raw_runtime_now_us(void* context) {
    const spw_raw_runtime_context_t* runtime =
        (const spw_raw_runtime_context_t*)context;
    return runtime->ops->now_us(runtime->context);
}

static spw_result_t raw_runtime_delay_us(void* context,
                                         uint64_t delay_us,
                                         spw_timeout_us_t timeout_us) {
    spw_raw_runtime_context_t* runtime =
        (spw_raw_runtime_context_t*)context;
    return runtime->ops->delay_us(runtime->context, delay_us, timeout_us);
}

static const spw_vspw_runtime_ops_t RAW_RUNTIME_OPS = {
    raw_runtime_now_us,
    raw_runtime_delay_us
};

static uint64_t session_nonce(const spw_raw_ethernet_backend_t* backend) {
    uint64_t value = (uint64_t)(uintptr_t)backend ^
                     ((uint64_t)backend->config.link_id << 32u);
    size_t i;
    for (i = 0u; i < SPW_RAW_ETHERNET_MAC_SIZE; ++i) {
        value ^= (uint64_t)backend->config.local_mac[i] << ((i % 8u) * 8u);
        value ^= (uint64_t)backend->config.remote_mac[i]
                 << (((i + 2u) % 8u) * 8u);
    }
    return value == 0u ? 1u : value;
}

static spw_result_t raw_construct(void* context,
                                  const spw_port_config_t* port_config) {
    spw_raw_ethernet_backend_t* backend =
        (spw_raw_ethernet_backend_t*)context;
    const spw_raw_ethernet_config_t* config =
        (const spw_raw_ethernet_config_t*)port_config->backend_config;
    spw_vspw_engine_config_t engine_config;
    spw_result_t result;

    memset(backend, 0, sizeof(*backend));
    backend->config = *config;
    backend->transport =
        (spw_transport_provider_t)SPW_TRANSPORT_PROVIDER_INITIALIZER;
    backend->remote_peer =
        (spw_transport_peer_id_t)SPW_TRANSPORT_PEER_ID_INITIALIZER;

    backend->runtime_context.ops = config->runtime_ops;
    backend->runtime_context.context = config->runtime_context;
    backend->runtime.ops = &RAW_RUNTIME_OPS;
    backend->runtime.context = &backend->runtime_context;

    result = spw_raw_ethernet_transport_init(
        &backend->transport_context, config, &backend->runtime);
    if (result != SPW_OK) {
        return result;
    }
    spw_raw_ethernet_transport_provider(
        &backend->transport_context, &backend->transport);
    result = spw_raw_ethernet_transport_remote_peer(
        &backend->transport_context, &backend->remote_peer);
    if (result != SPW_OK) {
        return result;
    }

    memset(&engine_config, 0, sizeof(engine_config));
    engine_config.link_id = config->link_id;
    engine_config.fragment_payload_size = config->fragment_payload_size;
    engine_config.max_retries = config->max_retries;
    engine_config.ack_timeout_ms = config->ack_timeout_ms;
    engine_config.keepalive_interval_ms = config->keepalive_interval_ms;
    engine_config.peer_timeout_ms = config->peer_timeout_ms;
    engine_config.virtual_link_bps = config->virtual_link_bps;
    engine_config.virtual_latency_us = config->virtual_latency_us;
    engine_config.session_nonce = session_nonce(backend);

    return spw_vspw_engine_init(
        &backend->engine, &engine_config, &backend->transport,
        &backend->remote_peer, &backend->runtime, NULL);
}

static void raw_destroy(void* context) {
    (void)context;
}

static spw_result_t raw_start(void* context) {
    return spw_vspw_engine_start(
        &((spw_raw_ethernet_backend_t*)context)->engine);
}

static spw_result_t raw_stop(void* context) {
    return spw_vspw_engine_stop(
        &((spw_raw_ethernet_backend_t*)context)->engine);
}

static spw_result_t raw_reset(void* context) {
    return spw_vspw_engine_reset(
        &((spw_raw_ethernet_backend_t*)context)->engine);
}

static spw_result_t raw_get_link_state(const void* context,
                                       spw_link_state_t* out_state) {
    spw_raw_ethernet_backend_t* backend =
        (spw_raw_ethernet_backend_t*)(uintptr_t)context;
    return spw_vspw_engine_get_link_state(&backend->engine, out_state);
}

static spw_result_t raw_get_capabilities(
    const void* context,
    spw_capabilities_t* out_capabilities) {
    const spw_raw_ethernet_backend_t* backend =
        (const spw_raw_ethernet_backend_t*)context;
    return spw_vspw_engine_get_capabilities(
        &backend->engine, out_capabilities);
}

static bool raw_supports_zero_copy(const void* context) {
    (void)context;
    return false;
}

static spw_result_t raw_send(void* context,
                             const spw_packet_t* packet,
                             spw_timeout_us_t timeout_us) {
    return spw_vspw_engine_send(
        &((spw_raw_ethernet_backend_t*)context)->engine,
        packet, timeout_us);
}

static spw_result_t raw_receive(void* context,
                                spw_packet_t* packet,
                                spw_timeout_us_t timeout_us) {
    return spw_vspw_engine_receive(
        &((spw_raw_ethernet_backend_t*)context)->engine,
        packet, timeout_us);
}

static spw_result_t raw_send_time_code(void* context,
                                       const spw_time_code_t* time_code,
                                       spw_timeout_us_t timeout_us) {
    return spw_vspw_engine_send_time_code(
        &((spw_raw_ethernet_backend_t*)context)->engine,
        time_code, timeout_us);
}

static spw_result_t raw_receive_time_code(void* context,
                                          spw_time_code_t* time_code,
                                          spw_timeout_us_t timeout_us) {
    return spw_vspw_engine_receive_time_code(
        &((spw_raw_ethernet_backend_t*)context)->engine,
        time_code, timeout_us);
}

static spw_result_t raw_get_statistics(
    const void* context,
    spw_statistics_t* out_statistics) {
    const spw_raw_ethernet_backend_t* backend =
        (const spw_raw_ethernet_backend_t*)context;
    return spw_vspw_engine_get_statistics(
        &backend->engine, out_statistics);
}

static spw_result_t raw_clear_statistics(void* context) {
    return spw_vspw_engine_clear_statistics(
        &((spw_raw_ethernet_backend_t*)context)->engine);
}

static const spw_backend_ops_t RAW_BACKEND_OPS = {
    raw_start,
    raw_stop,
    raw_reset,
    raw_get_link_state,
    raw_get_capabilities,
    raw_supports_zero_copy,
    raw_send,
    raw_receive,
    raw_send_time_code,
    raw_receive_time_code,
    raw_get_statistics,
    raw_clear_statistics,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static const spw_backend_factory_t RAW_FACTORY = {
    sizeof(spw_raw_ethernet_backend_t),
    alignof(spw_raw_ethernet_backend_t),
    raw_construct,
    raw_destroy,
    &RAW_BACKEND_OPS,
    NULL
};

const spw_backend_factory_t* spw_raw_ethernet_backend_factory(void) {
    return &RAW_FACTORY;
}
