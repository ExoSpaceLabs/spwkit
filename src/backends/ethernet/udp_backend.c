// SPDX-License-Identifier: Apache-2.0

#include "backends/ethernet/udp_backend.h"
#include "backends/ethernet/deterministic_faults.h"
#include "backends/ethernet/udp_transport_provider.h"
#include "backends/ethernet/vspw_engine.h"
#include "backends/ethernet/vspw_fault_transport.h"
#include "platform/host_vspw_runtime.h"

#include <spwkit/udp.h>

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

typedef struct spw_udp_backend {
    spw_udp_config_t config;

    spw_udp_transport_t udp_transport_context;
    spw_transport_provider_t udp_transport;

    spw_host_vspw_runtime_context_t runtime_context;
    spw_vspw_runtime_t runtime;

    spw_deterministic_fault_injector_t fault_injector;
    spw_fault_statistics_t fault_statistics;
    spw_vspw_fault_transport_t fault_transport_context;
    spw_transport_provider_t fault_transport;

    spw_transport_peer_id_t remote_peer;
    spw_vspw_engine_t engine;
} spw_udp_backend_t;

static bool valid_config(const spw_udp_config_t* config) {
    size_t i;
    if (config == NULL || config->version != SPW_UDP_CONFIG_VERSION ||
        config->struct_size < sizeof(spw_udp_config_t) ||
        config->remote_port == 0u || config->link_id == 0u ||
        config->fragment_payload_size < 256u ||
        config->fragment_payload_size > SPW_VSPW_TP_MAX_FRAGMENT_PAYLOAD ||
        config->max_retries == 0u || config->ack_timeout_ms == 0u ||
        config->keepalive_interval_ms == 0u ||
        config->peer_timeout_ms <= config->keepalive_interval_ms ||
        config->reserved != 0u) {
        return false;
    }
    for (i = 0u; i < SPW_UDP_FAULT_RULE_COUNT; ++i) {
        if (!spw_fault_rule_valid(&config->fault_rules[i])) {
            return false;
        }
    }
    return true;
}

static uint64_t make_session_nonce(const spw_udp_backend_t* backend) {
    uint64_t value =
        ((uint64_t)(unsigned)getpid() << 32u) ^
        ((uint64_t)backend->config.local_port << 16u) ^
        (uint64_t)backend->config.remote_port ^
        (uint64_t)(uintptr_t)backend;
    return value == 0u ? 1u : value;
}

static spw_terminator_t select_tx_terminator(
    void* context,
    spw_terminator_t requested) {
    spw_udp_backend_t* backend = (spw_udp_backend_t*)context;
    if (requested == SPW_TERMINATOR_EOP &&
        spw_fault_inject_spacewire_eep(&backend->fault_injector)) {
        ++backend->fault_statistics.spacewire_eep_injections;
        return SPW_TERMINATOR_EEP;
    }
    return requested;
}

static spw_result_t udp_construct(void* context,
                                  const spw_port_config_t* port_config) {
    spw_udp_backend_t* backend = (spw_udp_backend_t*)context;
    const spw_udp_config_t* config;
    spw_vspw_engine_config_t engine_config;
    spw_vspw_engine_hooks_t hooks = SPW_VSPW_ENGINE_HOOKS_INITIALIZER;
    spw_result_t result;

    if (backend == NULL || port_config == NULL ||
        port_config->backend_config == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    config = (const spw_udp_config_t*)port_config->backend_config;
    if (!valid_config(config)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    memset(backend, 0, sizeof(*backend));
    memcpy(&backend->config, config, sizeof(*config));

    backend->udp_transport =
        (spw_transport_provider_t)SPW_TRANSPORT_PROVIDER_INITIALIZER;
    backend->fault_transport =
        (spw_transport_provider_t)SPW_TRANSPORT_PROVIDER_INITIALIZER;
    backend->runtime =
        (spw_vspw_runtime_t)SPW_VSPW_RUNTIME_INITIALIZER;
    backend->remote_peer =
        (spw_transport_peer_id_t)SPW_TRANSPORT_PEER_ID_INITIALIZER;

    spw_host_vspw_runtime_init(&backend->runtime_context, &backend->runtime);
    if (!spw_vspw_runtime_valid(&backend->runtime)) {
        return SPW_ERR_BACKEND;
    }

    result = spw_udp_transport_init(&backend->udp_transport_context, config);
    if (result != SPW_OK) {
        return result;
    }
    spw_udp_transport_provider(&backend->udp_transport_context,
                               &backend->udp_transport);
    result = spw_udp_transport_remote_peer(
        &backend->udp_transport_context, &backend->remote_peer);
    if (result != SPW_OK) {
        spw_udp_transport_destroy(&backend->udp_transport_context);
        return result;
    }

    spw_fault_injector_init(&backend->fault_injector, config);
    result = spw_vspw_fault_transport_init(
        &backend->fault_transport_context,
        &backend->udp_transport,
        &backend->runtime,
        &backend->fault_injector,
        &backend->engine.statistics,
        &backend->fault_statistics);
    if (result != SPW_OK) {
        spw_udp_transport_destroy(&backend->udp_transport_context);
        return result;
    }
    spw_vspw_fault_transport_provider(
        &backend->fault_transport_context, &backend->fault_transport);

    memset(&engine_config, 0, sizeof(engine_config));
    engine_config.link_id = config->link_id;
    engine_config.fragment_payload_size = config->fragment_payload_size;
    engine_config.max_retries = config->max_retries;
    engine_config.ack_timeout_ms = config->ack_timeout_ms;
    engine_config.keepalive_interval_ms = config->keepalive_interval_ms;
    engine_config.peer_timeout_ms = config->peer_timeout_ms;
    engine_config.virtual_link_bps = config->virtual_link_bps;
    engine_config.virtual_latency_us = config->virtual_latency_us;
    engine_config.session_nonce = make_session_nonce(backend);

    hooks.context = backend;
    hooks.select_tx_terminator = select_tx_terminator;

    result = spw_vspw_engine_init(
        &backend->engine, &engine_config, &backend->fault_transport,
        &backend->remote_peer, &backend->runtime, &hooks);
    if (result != SPW_OK) {
        spw_udp_transport_destroy(&backend->udp_transport_context);
        backend->udp_transport =
            (spw_transport_provider_t)SPW_TRANSPORT_PROVIDER_INITIALIZER;
        backend->fault_transport =
            (spw_transport_provider_t)SPW_TRANSPORT_PROVIDER_INITIALIZER;
        return result;
    }
    return SPW_OK;
}

static void udp_destroy(void* context) {
    spw_udp_backend_t* backend = (spw_udp_backend_t*)context;
    if (backend == NULL) {
        return;
    }
    spw_udp_transport_destroy(&backend->udp_transport_context);
    backend->udp_transport =
        (spw_transport_provider_t)SPW_TRANSPORT_PROVIDER_INITIALIZER;
    backend->fault_transport =
        (spw_transport_provider_t)SPW_TRANSPORT_PROVIDER_INITIALIZER;
}

static spw_result_t udp_start(void* context) {
    spw_udp_backend_t* backend = (spw_udp_backend_t*)context;
    spw_fault_injector_reset(&backend->fault_injector);
    return spw_vspw_engine_start(&backend->engine);
}

static spw_result_t udp_stop(void* context) {
    spw_udp_backend_t* backend = (spw_udp_backend_t*)context;
    return spw_vspw_engine_stop(&backend->engine);
}

static spw_result_t udp_reset(void* context) {
    spw_udp_backend_t* backend = (spw_udp_backend_t*)context;
    spw_fault_injector_reset(&backend->fault_injector);
    return spw_vspw_engine_reset(&backend->engine);
}

static spw_result_t udp_get_link_state(const void* context,
                                       spw_link_state_t* out_state) {
    spw_udp_backend_t* backend =
        (spw_udp_backend_t*)(uintptr_t)context;
    return spw_vspw_engine_get_link_state(&backend->engine, out_state);
}

static spw_result_t udp_get_capabilities(
    const void* context,
    spw_capabilities_t* out_capabilities) {
    const spw_udp_backend_t* backend =
        (const spw_udp_backend_t*)context;
    spw_result_t result = spw_vspw_engine_get_capabilities(
        &backend->engine, out_capabilities);
    if (result == SPW_OK) {
        out_capabilities->bits |= SPW_CAP_FAULT_INJECTION;
    }
    return result;
}

static bool udp_supports_zero_copy(const void* context) {
    (void)context;
    return false;
}

static spw_result_t udp_send(void* context,
                             const spw_packet_t* packet,
                             spw_timeout_us_t timeout_us) {
    spw_udp_backend_t* backend = (spw_udp_backend_t*)context;
    return spw_vspw_engine_send(&backend->engine, packet, timeout_us);
}

static spw_result_t udp_receive(void* context,
                                spw_packet_t* packet,
                                spw_timeout_us_t timeout_us) {
    spw_udp_backend_t* backend = (spw_udp_backend_t*)context;
    return spw_vspw_engine_receive(&backend->engine, packet, timeout_us);
}

static spw_result_t udp_send_time_code(void* context,
                                       const spw_time_code_t* time_code,
                                       spw_timeout_us_t timeout_us) {
    spw_udp_backend_t* backend = (spw_udp_backend_t*)context;
    return spw_vspw_engine_send_time_code(
        &backend->engine, time_code, timeout_us);
}

static spw_result_t udp_receive_time_code(void* context,
                                          spw_time_code_t* time_code,
                                          spw_timeout_us_t timeout_us) {
    spw_udp_backend_t* backend = (spw_udp_backend_t*)context;
    return spw_vspw_engine_receive_time_code(
        &backend->engine, time_code, timeout_us);
}

static spw_result_t udp_get_statistics(
    const void* context,
    spw_statistics_t* out_statistics) {
    const spw_udp_backend_t* backend =
        (const spw_udp_backend_t*)context;
    return spw_vspw_engine_get_statistics(
        &backend->engine, out_statistics);
}

static spw_result_t udp_clear_statistics(void* context) {
    spw_udp_backend_t* backend = (spw_udp_backend_t*)context;
    return spw_vspw_engine_clear_statistics(&backend->engine);
}

static spw_result_t udp_get_fault_statistics(
    const void* context,
    spw_fault_statistics_t* out_statistics) {
    const spw_udp_backend_t* backend =
        (const spw_udp_backend_t*)context;
    if (out_statistics == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_statistics = backend->fault_statistics;
    return SPW_OK;
}

static spw_result_t udp_clear_fault_statistics(void* context) {
    spw_udp_backend_t* backend = (spw_udp_backend_t*)context;
    memset(&backend->fault_statistics, 0, sizeof(backend->fault_statistics));
    return SPW_OK;
}

static const spw_backend_ops_t UDP_OPS = {
    udp_start,
    udp_stop,
    udp_reset,
    udp_get_link_state,
    udp_get_capabilities,
    udp_supports_zero_copy,
    udp_send,
    udp_receive,
    udp_send_time_code,
    udp_receive_time_code,
    udp_get_statistics,
    udp_clear_statistics,
    udp_get_fault_statistics,
    udp_clear_fault_statistics,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static const spw_backend_factory_t UDP_FACTORY = {
    sizeof(spw_udp_backend_t),
    alignof(spw_udp_backend_t),
    udp_construct,
    udp_destroy,
    &UDP_OPS,
    NULL
};

const spw_backend_factory_t* spw_udp_backend_factory(void) {
    return &UDP_FACTORY;
}
