// SPDX-License-Identifier: Apache-2.0

#include <spwkit/buffer.h>
#include <spwkit/device.h>
#include <spwkit/driver.h>
#include <spwkit/port.h>
#include <spwkit/simulator.h>
#include <spwkit/udp.h>

#include "backends/driver/driver_backend.h"
#include "backends/loopback/loopback_backend.h"
#ifdef SPWKIT_HAS_DEVICE
#include "backends/device/device_backend.h"
#endif
#ifdef SPWKIT_HAS_SIMULATOR
#include "backends/virtual/simulator_backend.h"
#endif
#ifdef SPWKIT_HAS_UDP
#include "backends/ethernet/udp_backend.h"
#endif
#include "core/backend_c.h"
#include "core/buffer_internal.h"

#include <stdbool.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

#ifndef SPWKIT_ENABLE_HEAP
#define SPWKIT_ENABLE_HEAP 1
#endif

struct spw_port {
    const spw_backend_ops_t* ops;
    void* backend_context;
    void (*destroy_backend)(void* context);
    void* workspace_base;
    size_t workspace_alignment;
    bool release_workspace;
};

static size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static bool is_aligned(const void* pointer, size_t alignment) {
    return pointer != NULL &&
           ((uintptr_t)pointer % (uintptr_t)alignment) == 0u;
}

static void* backend_address(void* workspace, size_t backend_alignment) {
    uint8_t* bytes = (uint8_t*)workspace;
    return bytes + align_up(sizeof(spw_port_t), backend_alignment);
}

static spw_port_workspace_requirements_t requirements_for(
    const spw_backend_factory_t* factory) {
    const size_t port_alignment = alignof(spw_port_t);
    const size_t required_alignment =
        factory->context_alignment > port_alignment
            ? factory->context_alignment
            : port_alignment;
    const size_t context_offset =
        align_up(sizeof(spw_port_t), factory->context_alignment);
    spw_port_workspace_requirements_t requirements;
    requirements.size = context_offset + factory->context_size;
    requirements.alignment = required_alignment;
    return requirements;
}

static spw_result_t validate_common_config(const spw_port_config_t* config) {
    if (config == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (config->struct_size < sizeof(spw_port_config_t)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (config->version != SPW_PORT_CONFIG_VERSION) {
        return SPW_ERR_UNSUPPORTED;
    }
    if (config->flags != 0u) {
        return SPW_ERR_UNSUPPORTED;
    }
    return SPW_OK;
}

static spw_result_t validate_device_config(const spw_port_config_t* config) {
    const spw_device_config_t* device;
    size_t endpoint_length;
    if (config->backend_config == NULL ||
        config->backend_config_size < sizeof(spw_device_config_t)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    device = (const spw_device_config_t*)config->backend_config;
    if (device->struct_size < sizeof(spw_device_config_t)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (device->version != SPW_DEVICE_CONFIG_VERSION) {
        return SPW_ERR_UNSUPPORTED;
    }
    if (device->reserved != 0u || device->endpoint[0] == '\0') {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    endpoint_length = 0u;
    while (endpoint_length < SPW_DEVICE_ENDPOINT_CAPACITY &&
           device->endpoint[endpoint_length] != '\0') {
        ++endpoint_length;
    }
    if (endpoint_length == 0u || endpoint_length >= SPW_DEVICE_ENDPOINT_CAPACITY) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return SPW_OK;
}

static spw_result_t validate_driver_config(const spw_port_config_t* config) {
    const spw_driver_config_t* driver;
    const spw_driver_ops_t* ops;
    if (config->backend_config == NULL ||
        config->backend_config_size < sizeof(spw_driver_config_t)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    driver = (const spw_driver_config_t*)config->backend_config;
    if (driver->struct_size < sizeof(spw_driver_config_t)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (driver->version != SPW_DRIVER_CONFIG_VERSION) {
        return SPW_ERR_UNSUPPORTED;
    }
    if (driver->reserved != 0u || driver->ops == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    ops = driver->ops;
    if (ops->struct_size < sizeof(spw_driver_ops_t)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (ops->version != SPW_DRIVER_OPS_VERSION) {
        return SPW_ERR_UNSUPPORTED;
    }
    if (ops->start == NULL || ops->stop == NULL || ops->reset == NULL ||
        ops->get_link_state == NULL || ops->get_capabilities == NULL ||
        ops->send == NULL || ops->receive == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return SPW_OK;
}

static spw_result_t validate_simulator_config(const spw_port_config_t* config) {
    const spw_simulator_config_t* simulator;
    if (config->backend_config == NULL ||
        config->backend_config_size < sizeof(spw_simulator_config_t)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    simulator = (const spw_simulator_config_t*)config->backend_config;
    if (simulator->struct_size < sizeof(spw_simulator_config_t)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (simulator->version != SPW_SIMULATOR_CONFIG_VERSION) {
        return SPW_ERR_UNSUPPORTED;
    }
    if (simulator->endpoint != SPW_SIMULATOR_ENDPOINT_A &&
        simulator->endpoint != SPW_SIMULATOR_ENDPOINT_B) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return SPW_OK;
}

static bool valid_udp_fault_rule(const spw_udp_fault_rule_t* rule) {
    if (rule->reserved != 0u ||
        rule->probability_per_10000 > SPW_UDP_FAULT_PROBABILITY_SCALE ||
        rule->target > SPW_UDP_FAULT_TARGET_KEEPALIVE ||
        rule->action > SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP) {
        return false;
    }
    if (rule->action == SPW_UDP_FAULT_ACTION_NONE) {
        return rule->target == SPW_UDP_FAULT_TARGET_ANY &&
               rule->probability_per_10000 == 0u && rule->max_events == 0u &&
               rule->delay_us == 0u;
    }
    if (rule->probability_per_10000 == 0u) {
        return false;
    }
    if (rule->action == SPW_UDP_FAULT_ACTION_TRANSPORT_DELAY) {
        return rule->delay_us != 0u;
    }
    if (rule->delay_us != 0u) {
        return false;
    }
    if (rule->action == SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP) {
        return rule->target == SPW_UDP_FAULT_TARGET_DATA;
    }
    return true;
}

static spw_result_t validate_udp_config(const spw_port_config_t* config) {
    const spw_udp_config_t* udp;
    size_t i;
    if (config->backend_config == NULL ||
        config->backend_config_size < sizeof(spw_udp_config_t)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    udp = (const spw_udp_config_t*)config->backend_config;
    if (udp->struct_size < sizeof(spw_udp_config_t)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (udp->version != SPW_UDP_CONFIG_VERSION) {
        return SPW_ERR_UNSUPPORTED;
    }
    if (udp->remote_port == 0u || udp->link_id == 0u ||
        udp->fragment_payload_size < 256u || udp->max_retries == 0u ||
        udp->ack_timeout_ms == 0u || udp->keepalive_interval_ms == 0u ||
        udp->peer_timeout_ms <= udp->keepalive_interval_ms ||
        udp->reserved != 0u || udp->local_address[0] == '\0' ||
        udp->remote_address[0] == '\0') {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    for (i = 0u; i < SPW_UDP_FAULT_RULE_COUNT; ++i) {
        if (!valid_udp_fault_rule(&udp->fault_rules[i])) {
            return SPW_ERR_INVALID_ARGUMENT;
        }
    }
    return SPW_OK;
}

static spw_result_t select_factory(
    const spw_port_config_t* config,
    const spw_backend_factory_t** out_factory) {
    spw_result_t result = validate_common_config(config);
    if (result != SPW_OK) {
        return result;
    }

    *out_factory = NULL;
    switch (config->backend) {
    case SPW_BACKEND_LOOPBACK:
        if (config->backend_config != NULL || config->backend_config_size != 0u) {
            return SPW_ERR_INVALID_ARGUMENT;
        }
        *out_factory = spw_loopback_backend_factory();
        return SPW_OK;

    case SPW_BACKEND_SIMULATOR:
        result = validate_simulator_config(config);
        if (result != SPW_OK) {
            return result;
        }
#ifdef SPWKIT_HAS_SIMULATOR
        *out_factory = spw_simulator_backend_factory();
        return SPW_OK;
#else
        return SPW_ERR_UNSUPPORTED;
#endif

    case SPW_BACKEND_UDP:
        result = validate_udp_config(config);
        if (result != SPW_OK) {
            return result;
        }
#ifdef SPWKIT_HAS_UDP
        *out_factory = spw_udp_backend_factory();
        return SPW_OK;
#else
        return SPW_ERR_UNSUPPORTED;
#endif

    case SPW_BACKEND_DEVICE:
        result = validate_device_config(config);
        if (result != SPW_OK) {
            return result;
        }
#ifdef SPWKIT_HAS_DEVICE
        *out_factory = spw_device_backend_factory();
        return SPW_OK;
#else
        return SPW_ERR_UNSUPPORTED;
#endif

    case SPW_BACKEND_DRIVER: {
        const spw_driver_config_t* driver;
        result = validate_driver_config(config);
        if (result != SPW_OK) {
            return result;
        }
        driver = (const spw_driver_config_t*)config->backend_config;
        *out_factory = spw_driver_backend_factory(driver->ops->wait != NULL);
        return SPW_OK;
    }

    default:
        return SPW_ERR_UNSUPPORTED;
    }
}

static spw_result_t validate_port(const spw_port_t* port) {
    return port != NULL && port->ops != NULL && port->backend_context != NULL
               ? SPW_OK
               : SPW_ERR_INVALID_ARGUMENT;
}

static bool valid_terminator(spw_terminator_t terminator) {
    return terminator == SPW_TERMINATOR_EOP || terminator == SPW_TERMINATOR_EEP;
}

static bool application_owned(const spw_buffer_t* buffer) {
    const struct spw_buffer* internal = (const struct spw_buffer*)buffer;
    return internal != NULL && internal->state == SPW_BUFFER_STATE_APPLICATION;
}

#if SPWKIT_ENABLE_HEAP
static void* allocate_workspace(size_t size, size_t alignment) {
#if defined(_MSC_VER)
    return _aligned_malloc(size, alignment);
#else
    const size_t rounded_size = align_up(size, alignment);
    return aligned_alloc(alignment, rounded_size);
#endif
}

static void free_workspace(void* workspace) {
#if defined(_MSC_VER)
    _aligned_free(workspace);
#else
    free(workspace);
#endif
}
#endif

static spw_result_t unsupported_fault_statistics(
    spw_fault_statistics_t* out_statistics) {
    memset(out_statistics, 0, sizeof(*out_statistics));
    return SPW_ERR_UNSUPPORTED;
}

spw_result_t spw_port_workspace_requirements(
    const spw_port_config_t* config,
    spw_port_workspace_requirements_t* out_requirements) {
    const spw_backend_factory_t* factory = NULL;
    spw_result_t result;

    if (out_requirements == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    out_requirements->size = 0u;
    out_requirements->alignment = 0u;

    result = select_factory(config, &factory);
    if (result != SPW_OK) {
        return result;
    }
    if (factory == NULL || factory->ops == NULL || factory->construct == NULL ||
        factory->destroy == NULL || factory->context_size == 0u ||
        factory->context_alignment == 0u) {
        return SPW_ERR_BACKEND;
    }

    *out_requirements = requirements_for(factory);
    return SPW_OK;
}

spw_result_t spw_port_open_in_place(const spw_port_config_t* config,
                                    void* workspace,
                                    size_t workspace_size,
                                    spw_port_t** out_port) {
    spw_port_workspace_requirements_t requirements;
    const spw_backend_factory_t* factory = NULL;
    spw_port_t* port;
    void* context;
    spw_result_t result;

    if (out_port == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_port = NULL;

    result = select_factory(config, &factory);
    if (result != SPW_OK) {
        return result;
    }
    requirements = requirements_for(factory);

    if (workspace == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!is_aligned(workspace, requirements.alignment)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (workspace_size < requirements.size) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }

    context = backend_address(workspace, factory->context_alignment);
    result = factory->construct(context, config);
    if (result != SPW_OK) {
        return result;
    }

    port = (spw_port_t*)workspace;
    port->ops = factory->ops;
    port->backend_context = context;
    port->destroy_backend = factory->destroy;
    port->workspace_base = workspace;
    port->workspace_alignment = requirements.alignment;
    port->release_workspace = false;

    *out_port = port;
    return SPW_OK;
}

spw_result_t spw_port_open(const spw_port_config_t* config, spw_port_t** out_port) {
    spw_port_workspace_requirements_t requirements;
    spw_port_t* port = NULL;
    spw_result_t result;
#if SPWKIT_ENABLE_HEAP
    void* workspace;
#endif

    if (out_port == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_port = NULL;

    result = spw_port_workspace_requirements(config, &requirements);
    if (result != SPW_OK) {
        return result;
    }

#if SPWKIT_ENABLE_HEAP
    workspace = allocate_workspace(requirements.size, requirements.alignment);
    if (workspace == NULL) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    result = spw_port_open_in_place(config, workspace, requirements.size, &port);
    if (result != SPW_OK) {
        free_workspace(workspace);
        return result;
    }

    port->release_workspace = true;
    *out_port = port;
    return SPW_OK;
#else
    (void)requirements;
    (void)port;
    return SPW_ERR_UNSUPPORTED;
#endif
}

spw_result_t spw_port_close(spw_port_t* port) {
    void* workspace;
    bool release_workspace;
    void* context;
    void (*destroy)(void* context);

    if (validate_port(port) != SPW_OK) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    workspace = port->workspace_base;
    release_workspace = port->release_workspace;
    context = port->backend_context;
    destroy = port->destroy_backend;

    port->ops = NULL;
    port->backend_context = NULL;
    if (destroy != NULL) {
        destroy(context);
    }

#if SPWKIT_ENABLE_HEAP
    if (release_workspace) {
        free_workspace(workspace);
    }
#else
    (void)workspace;
    (void)release_workspace;
#endif
    return SPW_OK;
}

spw_result_t spw_port_start(spw_port_t* port) {
    return validate_port(port) == SPW_OK
               ? port->ops->start(port->backend_context)
               : SPW_ERR_INVALID_ARGUMENT;
}

spw_result_t spw_port_stop(spw_port_t* port) {
    return validate_port(port) == SPW_OK
               ? port->ops->stop(port->backend_context)
               : SPW_ERR_INVALID_ARGUMENT;
}

spw_result_t spw_port_reset(spw_port_t* port) {
    return validate_port(port) == SPW_OK
               ? port->ops->reset(port->backend_context)
               : SPW_ERR_INVALID_ARGUMENT;
}

spw_result_t spw_port_get_link_state(const spw_port_t* port,
                                     spw_link_state_t* out_state) {
    if (validate_port(port) != SPW_OK || out_state == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->ops->get_link_state(port->backend_context, out_state);
}

spw_result_t spw_port_get_capabilities(const spw_port_t* port,
                                       spw_capabilities_t* out_capabilities) {
    spw_result_t result;
    if (validate_port(port) != SPW_OK || out_capabilities == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    result = port->ops->get_capabilities(port->backend_context, out_capabilities);
    if (result == SPW_OK) {
        if (port->ops->supports_zero_copy != NULL &&
            port->ops->supports_zero_copy(port->backend_context)) {
            out_capabilities->bits |= SPW_CAP_ZERO_COPY;
        }
        if (port->ops->wait != NULL) {
            out_capabilities->bits |= SPW_CAP_READINESS;
        }
    }
    return result;
}

spw_result_t spw_port_wait(spw_port_t* port,
                           spw_ready_events_t interests,
                           spw_timeout_us_t timeout_us,
                           spw_ready_events_t* out_ready) {
    if (validate_port(port) != SPW_OK || out_ready == NULL ||
        interests == SPW_READY_NONE || (interests & ~SPW_READY_ALL) != 0u) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_ready = SPW_READY_NONE;
    if (port->ops->wait == NULL) {
        return SPW_ERR_UNSUPPORTED;
    }
    return port->ops->wait(port->backend_context,
                           interests,
                           timeout_us,
                           out_ready);
}

spw_result_t spw_port_send(spw_port_t* port,
                           const spw_packet_t* packet,
                           spw_timeout_us_t timeout_us) {
    if (validate_port(port) != SPW_OK || packet == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->ops->send(port->backend_context, packet, timeout_us);
}

spw_result_t spw_port_receive(spw_port_t* port,
                              spw_packet_t* packet,
                              spw_timeout_us_t timeout_us) {
    if (validate_port(port) != SPW_OK || packet == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->ops->receive(port->backend_context, packet, timeout_us);
}

spw_result_t spw_port_send_time_code(spw_port_t* port,
                                     const spw_time_code_t* time_code,
                                     spw_timeout_us_t timeout_us) {
    if (validate_port(port) != SPW_OK || time_code == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->ops->send_time_code(port->backend_context, time_code, timeout_us);
}

spw_result_t spw_port_receive_time_code(spw_port_t* port,
                                        spw_time_code_t* time_code,
                                        spw_timeout_us_t timeout_us) {
    if (validate_port(port) != SPW_OK || time_code == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->ops->receive_time_code(port->backend_context, time_code, timeout_us);
}

spw_result_t spw_port_get_statistics(const spw_port_t* port,
                                     spw_statistics_t* out_statistics) {
    if (validate_port(port) != SPW_OK || out_statistics == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->ops->get_statistics(port->backend_context, out_statistics);
}

spw_result_t spw_port_clear_statistics(spw_port_t* port) {
    return validate_port(port) == SPW_OK
               ? port->ops->clear_statistics(port->backend_context)
               : SPW_ERR_INVALID_ARGUMENT;
}

spw_result_t spw_port_get_fault_statistics(
    const spw_port_t* port,
    spw_fault_statistics_t* out_statistics) {
    if (validate_port(port) != SPW_OK || out_statistics == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (port->ops->get_fault_statistics == NULL) {
        return unsupported_fault_statistics(out_statistics);
    }
    return port->ops->get_fault_statistics(port->backend_context, out_statistics);
}

spw_result_t spw_port_clear_fault_statistics(spw_port_t* port) {
    if (validate_port(port) != SPW_OK) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (port->ops->clear_fault_statistics == NULL) {
        return SPW_ERR_UNSUPPORTED;
    }
    return port->ops->clear_fault_statistics(port->backend_context);
}

spw_result_t spw_buffer_get_view(const spw_buffer_t* buffer,
                                 spw_buffer_view_t* out_view) {
    const struct spw_buffer* internal;
    if (!application_owned(buffer) || out_view == NULL) {
        return SPW_ERR_INVALID_STATE;
    }
    internal = (const struct spw_buffer*)buffer;
    out_view->data = internal->data;
    out_view->length = internal->length;
    out_view->capacity = internal->capacity;
    out_view->terminator = internal->terminator;
    return SPW_OK;
}

spw_result_t spw_buffer_set_packet(spw_buffer_t* buffer,
                                   size_t length,
                                   spw_terminator_t terminator) {
    struct spw_buffer* internal;
    if (!application_owned(buffer)) {
        return SPW_ERR_INVALID_STATE;
    }
    internal = (struct spw_buffer*)buffer;
    if (internal->direction != SPW_BUFFER_DIRECTION_TX) {
        return SPW_ERR_INVALID_STATE;
    }
    if (length > internal->capacity || !valid_terminator(terminator)) {
        return SPW_ERR_INVALID_PACKET;
    }
    internal->length = length;
    internal->terminator = terminator;
    return SPW_OK;
}

spw_result_t spw_port_acquire_tx_buffer(spw_port_t* port,
                                        size_t min_capacity,
                                        spw_timeout_us_t timeout_us,
                                        spw_buffer_t** out_buffer) {
    if (validate_port(port) != SPW_OK || out_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_buffer = NULL;
    if (port->ops->acquire_tx_buffer == NULL) {
        return SPW_ERR_UNSUPPORTED;
    }
    return port->ops->acquire_tx_buffer(
        port->backend_context, min_capacity, timeout_us, out_buffer);
}

spw_result_t spw_port_submit_tx_buffer(spw_port_t* port,
                                       spw_buffer_t** inout_buffer,
                                       spw_timeout_us_t timeout_us) {
    spw_result_t result;
    if (validate_port(port) != SPW_OK || inout_buffer == NULL ||
        *inout_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (port->ops->submit_tx_buffer == NULL) {
        return SPW_ERR_UNSUPPORTED;
    }
    result = port->ops->submit_tx_buffer(
        port->backend_context, *inout_buffer, timeout_us);
    if (result == SPW_OK) {
        *inout_buffer = NULL;
    }
    return result;
}

spw_result_t spw_port_reclaim_tx_buffer(spw_port_t* port,
                                        spw_timeout_us_t timeout_us,
                                        spw_buffer_t** out_buffer) {
    if (validate_port(port) != SPW_OK || out_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_buffer = NULL;
    if (port->ops->reclaim_tx_buffer == NULL) {
        return SPW_ERR_UNSUPPORTED;
    }
    return port->ops->reclaim_tx_buffer(
        port->backend_context, timeout_us, out_buffer);
}

spw_result_t spw_port_release_tx_buffer(spw_port_t* port,
                                        spw_buffer_t** inout_buffer) {
    spw_result_t result;
    if (validate_port(port) != SPW_OK || inout_buffer == NULL ||
        *inout_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (port->ops->release_tx_buffer == NULL) {
        return SPW_ERR_UNSUPPORTED;
    }
    result = port->ops->release_tx_buffer(port->backend_context, *inout_buffer);
    if (result == SPW_OK) {
        *inout_buffer = NULL;
    }
    return result;
}

spw_result_t spw_port_acquire_rx_buffer(spw_port_t* port,
                                        spw_timeout_us_t timeout_us,
                                        spw_buffer_t** out_buffer) {
    if (validate_port(port) != SPW_OK || out_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_buffer = NULL;
    if (port->ops->acquire_rx_buffer == NULL) {
        return SPW_ERR_UNSUPPORTED;
    }
    return port->ops->acquire_rx_buffer(
        port->backend_context, timeout_us, out_buffer);
}

spw_result_t spw_port_release_rx_buffer(spw_port_t* port,
                                        spw_buffer_t** inout_buffer) {
    spw_result_t result;
    if (validate_port(port) != SPW_OK || inout_buffer == NULL ||
        *inout_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (port->ops->release_rx_buffer == NULL) {
        return SPW_ERR_UNSUPPORTED;
    }
    result = port->ops->release_rx_buffer(port->backend_context, *inout_buffer);
    if (result == SPW_OK) {
        *inout_buffer = NULL;
    }
    return result;
}
