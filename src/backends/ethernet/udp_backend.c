// SPDX-License-Identifier: Apache-2.0

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "backends/ethernet/udp_backend.h"
#include "backends/ethernet/deterministic_faults.h"
#include "backends/ethernet/udp_transport_provider.h"
#include "backends/ethernet/vspw_engine.h"
#include "backends/ethernet/vspw_runtime.h"

#include <spwkit/udp.h>

#include <errno.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

typedef struct spw_udp_backend {
    spw_vspw_engine_t engine;
    spw_udp_transport_t transport_context;
    spw_transport_provider_t transport;
    spw_transport_peer_id_t remote_peer;
    spw_vspw_runtime_t runtime;
    spw_deterministic_fault_injector_t fault_template;
} spw_udp_backend_t;

static uint64_t udp_runtime_now_us(void* context) {
    struct timespec now;
    (void)context;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0u;
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000);
}

static spw_result_t udp_runtime_delay_us(void* context,
                                         uint64_t delay_us,
                                         spw_timeout_us_t timeout_us) {
    struct timespec request;
    struct timespec remaining;
    (void)context;

    if (delay_us == 0u) {
        return SPW_OK;
    }
    if (timeout_us != SPW_TIMEOUT_INFINITE && timeout_us < delay_us) {
        return SPW_ERR_TIMEOUT;
    }

    request.tv_sec = (time_t)(delay_us / UINT64_C(1000000));
    request.tv_nsec = (long)((delay_us % UINT64_C(1000000)) * UINT64_C(1000));
    while (nanosleep(&request, &remaining) != 0) {
        if (errno != EINTR) {
            return SPW_ERR_BACKEND;
        }
        request = remaining;
    }
    return SPW_OK;
}

static const spw_vspw_runtime_ops_t UDP_RUNTIME_OPS = {
    udp_runtime_now_us,
    udp_runtime_delay_us
};

static spw_result_t udp_construct(void* context,
                                  const spw_port_config_t* port_config) {
    spw_udp_backend_t* backend = (spw_udp_backend_t*)context;
    const spw_udp_config_t* config =
        (const spw_udp_config_t*)port_config->backend_config;
    spw_vspw_engine_config_t engine_config;
    spw_result_t result;
    size_t i;

    memset(backend, 0, sizeof(*backend));
    backend->transport =
        (spw_transport_provider_t)SPW_TRANSPORT_PROVIDER_INITIALIZER;
    backend->remote_peer =
        (spw_transport_peer_id_t)SPW_TRANSPORT_PEER_ID_INITIALIZER;
    backend->runtime = (spw_vspw_runtime_t)SPW_VSPW_RUNTIME_INITIALIZER;

    if (config->version != SPW_UDP_CONFIG_VERSION ||
        config->struct_size < sizeof(spw_udp_config_t) ||
        config->remote_port == 0u || config->link_id == 0u ||
        config->fragment_payload_size < 256u ||
        config->fragment_payload_size > SPW_VSPW_TP_MAX_FRAGMENT_PAYLOAD ||
        config->max_retries == 0u || config->ack_timeout_ms == 0u ||
        config->keepalive_interval_ms == 0u ||
        config->peer_timeout_ms <= config->keepalive_interval_ms ||
        config->reserved != 0u) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    for (i = 0u; i < SPW_UDP_FAULT_RULE_COUNT; ++i) {
        if (!spw_fault_rule_valid(&config->fault_rules[i])) {
            return SPW_ERR_INVALID_ARGUMENT;
        }
    }

    result = spw_udp_transport_init(&backend->transport_context, config);
    if (result != SPW_OK) {
        return result;
    }
    spw_udp_transport_provider(&backend->transport_context,
                               &backend->transport);
    result = spw_udp_transport_remote_peer(&backend->transport_context,
                                           &backend->remote_peer);
    if (result != SPW_OK) {
        spw_udp_transport_destroy(&backend->transport_context);
        return result;
    }

    backend->runtime.ops = &UDP_RUNTIME_OPS;
    backend->runtime.context = backend;
    spw_fault_injector_init(&backend->fault_template, config);

    memset(&engine_config, 0, sizeof(engine_config));
    engine_config.link_id = config->link_id;
    engine_config.fragment_payload_size = config->fragment_payload_size;
    engine_config.max_retries = config->max_retries;
    engine_config.ack_timeout_ms = config->ack_timeout_ms;
    engine_config.keepalive_interval_ms = config->keepalive_interval_ms;
    engine_config.peer_timeout_ms = config->peer_timeout_ms;
    engine_config.virtual_link_bps = config->virtual_link_bps;
    engine_config.virtual_latency_us = config->virtual_latency_us;
    engine_config.session_salt =
        ((uint64_t)(unsigned)getpid() << 32u) ^
        ((uint64_t)config->local_port << 16u);

    result = spw_vspw_engine_init(
        &backend->engine, &engine_config, &backend->transport,
        &backend->remote_peer, &backend->runtime, &backend->fault_template);
    if (result != SPW_OK) {
        spw_udp_transport_destroy(&backend->transport_context);
        backend->transport =
            (spw_transport_provider_t)SPW_TRANSPORT_PROVIDER_INITIALIZER;
        return result;
    }
    return SPW_OK;
}

static void udp_destroy(void* context) {
    spw_udp_backend_t* backend = (spw_udp_backend_t*)context;
    spw_udp_transport_destroy(&backend->transport_context);
    backend->transport =
        (spw_transport_provider_t)SPW_TRANSPORT_PROVIDER_INITIALIZER;
}

static spw_result_t udp_start(void* context) {
    return spw_vspw_engine_start(&((spw_udp_backend_t*)context)->engine);
}

static spw_result_t udp_stop(void* context) {
    return spw_vspw_engine_stop(&((spw_udp_backend_t*)context)->engine);
}

static spw_result_t udp_reset(void* context) {
    return spw_vspw_engine_reset(&((spw_udp_backend_t*)context)->engine);
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
    const spw_udp_backend_t* backend = (const spw_udp_backend_t*)context;
    return spw_vspw_engine_get_capabilities(&backend->engine,
                                            out_capabilities);
}

static bool udp_supports_zero_copy(const void* context) {
    const spw_udp_backend_t* backend = (const spw_udp_backend_t*)context;
    return spw_vspw_engine_supports_zero_copy(&backend->engine);
}

static spw_result_t udp_send(void* context,
                             const spw_packet_t* packet,
                             spw_timeout_us_t timeout_us) {
    return spw_vspw_engine_send(&((spw_udp_backend_t*)context)->engine,
                                packet, timeout_us);
}

static spw_result_t udp_receive(void* context,
                                spw_packet_t* packet,
                                spw_timeout_us_t timeout_us) {
    return spw_vspw_engine_receive(&((spw_udp_backend_t*)context)->engine,
                                   packet, timeout_us);
}

static spw_result_t udp_send_time_code(void* context,
                                       const spw_time_code_t* time_code,
                                       spw_timeout_us_t timeout_us) {
    return spw_vspw_engine_send_time_code(
        &((spw_udp_backend_t*)context)->engine, time_code, timeout_us);
}

static spw_result_t udp_receive_time_code(void* context,
                                          spw_time_code_t* time_code,
                                          spw_timeout_us_t timeout_us) {
    return spw_vspw_engine_receive_time_code(
        &((spw_udp_backend_t*)context)->engine, time_code, timeout_us);
}

static spw_result_t udp_get_statistics(const void* context,
                                       spw_statistics_t* out_statistics) {
    const spw_udp_backend_t* backend = (const spw_udp_backend_t*)context;
    return spw_vspw_engine_get_statistics(&backend->engine, out_statistics);
}

static spw_result_t udp_clear_statistics(void* context) {
    return spw_vspw_engine_clear_statistics(
        &((spw_udp_backend_t*)context)->engine);
}

static spw_result_t udp_get_fault_statistics(
    const void* context,
    spw_fault_statistics_t* out_statistics) {
    const spw_udp_backend_t* backend = (const spw_udp_backend_t*)context;
    return spw_vspw_engine_get_fault_statistics(&backend->engine,
                                                out_statistics);
}

static spw_result_t udp_clear_fault_statistics(void* context) {
    return spw_vspw_engine_clear_fault_statistics(
        &((spw_udp_backend_t*)context)->engine);
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
