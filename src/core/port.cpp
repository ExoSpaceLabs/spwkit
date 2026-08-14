// SPDX-License-Identifier: Apache-2.0

#include <spwkit/buffer.h>
#include <spwkit/port.h>
#include <spwkit/simulator.h>

#include "backends/loopback/loopback_backend.hpp"
#include "core/backend.hpp"
#include "core/buffer.hpp"

#ifdef SPWKIT_HAS_SIMULATOR
#include "backends/virtual/simulator_backend.hpp"
#endif

#include <cstddef>
#include <cstdint>
#include <new>

#ifndef SPWKIT_ENABLE_HEAP
#define SPWKIT_ENABLE_HEAP 1
#endif

struct spw_port {
    spwkit::detail::Backend* backend;
    void (*destroy_backend)(spwkit::detail::Backend*) noexcept;
    void* workspace_base;
    std::size_t workspace_alignment;
    bool release_workspace;
};

namespace {

constexpr std::size_t align_up(std::size_t value, std::size_t alignment) noexcept {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

template <typename BackendType>
constexpr spw_port_workspace_requirements_t requirements_for() noexcept {
    constexpr std::size_t backend_alignment = alignof(BackendType);
    constexpr std::size_t port_alignment = alignof(spw_port_t);
    constexpr std::size_t required_alignment =
        backend_alignment > port_alignment ? backend_alignment : port_alignment;
    constexpr std::size_t backend_offset = align_up(sizeof(spw_port_t), backend_alignment);
    return {backend_offset + sizeof(BackendType), required_alignment};
}

template <typename BackendType>
BackendType* backend_address(void* workspace) noexcept {
    auto* bytes = static_cast<std::uint8_t*>(workspace);
    return reinterpret_cast<BackendType*>(
        bytes + align_up(sizeof(spw_port_t), alignof(BackendType)));
}

void destroy_loopback_in_place(spwkit::detail::Backend* backend) noexcept {
    static_cast<spwkit::detail::LoopbackBackend*>(backend)->~LoopbackBackend();
}

#ifdef SPWKIT_HAS_SIMULATOR
void destroy_simulator_in_place(spwkit::detail::Backend* backend) noexcept {
    static_cast<spwkit::detail::SimulatorBackend*>(backend)->~SimulatorBackend();
}
#endif

spw_result_t validate_common_config(const spw_port_config_t* config) noexcept {
    if (config == nullptr) {
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

spw_result_t validate_simulator_config(const spw_port_config_t* config) noexcept {
    if (config->backend_config == nullptr ||
        config->backend_config_size < sizeof(spw_simulator_config_t)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    const auto* simulator = static_cast<const spw_simulator_config_t*>(config->backend_config);
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

spw_result_t validate_port(const spw_port_t* port) noexcept {
    return (port != nullptr && port->backend != nullptr) ? SPW_OK : SPW_ERR_INVALID_ARGUMENT;
}

bool is_aligned(const void* pointer, std::size_t alignment) noexcept {
    return pointer != nullptr &&
           (reinterpret_cast<std::uintptr_t>(pointer) % alignment) == 0u;
}

bool valid_terminator(spw_terminator_t terminator) noexcept {
    return terminator == SPW_TERMINATOR_EOP || terminator == SPW_TERMINATOR_EEP;
}

bool application_owned(const spw_buffer_t* buffer) noexcept {
    return buffer != nullptr &&
           static_cast<const spw_buffer*>(buffer)->state ==
               spwkit::detail::BufferState::application;
}

} // namespace

extern "C" {

spw_result_t spw_port_workspace_requirements(
    const spw_port_config_t* config,
    spw_port_workspace_requirements_t* out_requirements) {
    if (out_requirements == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    out_requirements->size = 0u;
    out_requirements->alignment = 0u;

    const spw_result_t common = validate_common_config(config);
    if (common != SPW_OK) {
        return common;
    }

    switch (config->backend) {
    case SPW_BACKEND_LOOPBACK:
        if (config->backend_config != nullptr || config->backend_config_size != 0u) {
            return SPW_ERR_INVALID_ARGUMENT;
        }
        *out_requirements = requirements_for<spwkit::detail::LoopbackBackend>();
        return SPW_OK;

    case SPW_BACKEND_SIMULATOR: {
        const spw_result_t result = validate_simulator_config(config);
        if (result != SPW_OK) {
            return result;
        }
#ifdef SPWKIT_HAS_SIMULATOR
        *out_requirements = requirements_for<spwkit::detail::SimulatorBackend>();
        return SPW_OK;
#else
        return SPW_ERR_UNSUPPORTED;
#endif
    }

    default:
        return SPW_ERR_UNSUPPORTED;
    }
}

spw_result_t spw_port_open_in_place(const spw_port_config_t* config,
                                    void* workspace,
                                    size_t workspace_size,
                                    spw_port_t** out_port) {
    if (out_port == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_port = nullptr;

    spw_port_workspace_requirements_t requirements{};
    const spw_result_t requirements_result =
        spw_port_workspace_requirements(config, &requirements);
    if (requirements_result != SPW_OK) {
        return requirements_result;
    }
    if (workspace == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!is_aligned(workspace, requirements.alignment)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (workspace_size < requirements.size) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }

    spwkit::detail::Backend* backend = nullptr;
    void (*destroy_backend)(spwkit::detail::Backend*) noexcept = nullptr;

    switch (config->backend) {
    case SPW_BACKEND_LOOPBACK: {
        auto* loopback = backend_address<spwkit::detail::LoopbackBackend>(workspace);
        backend = ::new (static_cast<void*>(loopback)) spwkit::detail::LoopbackBackend();
        destroy_backend = &destroy_loopback_in_place;
        break;
    }

    case SPW_BACKEND_SIMULATOR: {
#ifdef SPWKIT_HAS_SIMULATOR
        const auto simulator_config =
            *static_cast<const spw_simulator_config_t*>(config->backend_config);
        auto* simulator = backend_address<spwkit::detail::SimulatorBackend>(workspace);
        simulator = ::new (static_cast<void*>(simulator))
            spwkit::detail::SimulatorBackend(simulator_config);
        const spw_result_t attach_result = simulator->attach();
        if (attach_result != SPW_OK) {
            simulator->~SimulatorBackend();
            return attach_result;
        }
        backend = simulator;
        destroy_backend = &destroy_simulator_in_place;
        break;
#else
        return SPW_ERR_UNSUPPORTED;
#endif
    }

    default:
        return SPW_ERR_UNSUPPORTED;
    }

    auto* port = ::new (workspace) spw_port_t{
        backend,
        destroy_backend,
        workspace,
        requirements.alignment,
        false,
    };

    *out_port = port;
    return SPW_OK;
}

spw_result_t spw_port_open(const spw_port_config_t* config, spw_port_t** out_port) {
    if (out_port == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_port = nullptr;

    spw_port_workspace_requirements_t requirements{};
    const spw_result_t result = spw_port_workspace_requirements(config, &requirements);
    if (result != SPW_OK) {
        return result;
    }

#if SPWKIT_ENABLE_HEAP
    void* workspace = ::operator new(
        requirements.size,
        static_cast<std::align_val_t>(requirements.alignment),
        std::nothrow);
    if (workspace == nullptr) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    spw_port_t* port = nullptr;
    const spw_result_t open_result =
        spw_port_open_in_place(config, workspace, requirements.size, &port);
    if (open_result != SPW_OK) {
        ::operator delete(workspace,
                          static_cast<std::align_val_t>(requirements.alignment));
        return open_result;
    }

    port->release_workspace = true;
    *out_port = port;
    return SPW_OK;
#else
    (void)requirements;
    return SPW_ERR_UNSUPPORTED;
#endif
}

spw_result_t spw_port_close(spw_port_t* port) {
    if (validate_port(port) != SPW_OK) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    void* workspace = port->workspace_base;
    const std::size_t workspace_alignment = port->workspace_alignment;
    const bool release_workspace = port->release_workspace;
    auto* backend = port->backend;
    auto destroy_backend = port->destroy_backend;

    port->backend = nullptr;
    destroy_backend(backend);
    port->~spw_port();

#if SPWKIT_ENABLE_HEAP
    if (release_workspace) {
        ::operator delete(workspace,
                          static_cast<std::align_val_t>(workspace_alignment));
    }
#else
    (void)workspace;
    (void)workspace_alignment;
    (void)release_workspace;
#endif

    return SPW_OK;
}

spw_result_t spw_port_start(spw_port_t* port) {
    return validate_port(port) == SPW_OK ? port->backend->start() : SPW_ERR_INVALID_ARGUMENT;
}

spw_result_t spw_port_stop(spw_port_t* port) {
    return validate_port(port) == SPW_OK ? port->backend->stop() : SPW_ERR_INVALID_ARGUMENT;
}

spw_result_t spw_port_reset(spw_port_t* port) {
    return validate_port(port) == SPW_OK ? port->backend->reset() : SPW_ERR_INVALID_ARGUMENT;
}

spw_result_t spw_port_get_link_state(const spw_port_t* port, spw_link_state_t* out_state) {
    if (validate_port(port) != SPW_OK || out_state == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->backend->get_link_state(*out_state);
}

spw_result_t spw_port_get_capabilities(const spw_port_t* port,
                                       spw_capabilities_t* out_capabilities) {
    if (validate_port(port) != SPW_OK || out_capabilities == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    const spw_result_t result = port->backend->get_capabilities(*out_capabilities);
    if (result == SPW_OK && port->backend->supports_zero_copy()) {
        out_capabilities->bits |= SPW_CAP_ZERO_COPY;
    }
    return result;
}

spw_result_t spw_port_send(spw_port_t* port,
                           const spw_packet_t* packet,
                           spw_timeout_us_t timeout_us) {
    if (validate_port(port) != SPW_OK || packet == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->backend->send(*packet, timeout_us);
}

spw_result_t spw_port_receive(spw_port_t* port,
                              spw_packet_t* packet,
                              spw_timeout_us_t timeout_us) {
    if (validate_port(port) != SPW_OK || packet == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->backend->receive(*packet, timeout_us);
}

spw_result_t spw_buffer_get_view(const spw_buffer_t* buffer,
                                 spw_buffer_view_t* out_view) {
    if (!application_owned(buffer) || out_view == nullptr) {
        return SPW_ERR_INVALID_STATE;
    }
    const auto* internal = static_cast<const spw_buffer*>(buffer);
    out_view->data = internal->data;
    out_view->length = internal->length;
    out_view->capacity = internal->capacity;
    out_view->terminator = internal->terminator;
    return SPW_OK;
}

spw_result_t spw_buffer_set_packet(spw_buffer_t* buffer,
                                   size_t length,
                                   spw_terminator_t terminator) {
    if (!application_owned(buffer)) {
        return SPW_ERR_INVALID_STATE;
    }
    auto* internal = static_cast<spw_buffer*>(buffer);
    if (internal->direction != spwkit::detail::BufferDirection::tx) {
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
    if (validate_port(port) != SPW_OK || out_buffer == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_buffer = nullptr;
    return port->backend->acquire_tx_buffer(min_capacity, timeout_us, *out_buffer);
}

spw_result_t spw_port_submit_tx_buffer(spw_port_t* port,
                                       spw_buffer_t** inout_buffer,
                                       spw_timeout_us_t timeout_us) {
    if (validate_port(port) != SPW_OK || inout_buffer == nullptr ||
        *inout_buffer == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    const spw_result_t result =
        port->backend->submit_tx_buffer(**inout_buffer, timeout_us);
    if (result == SPW_OK) {
        *inout_buffer = nullptr;
    }
    return result;
}

spw_result_t spw_port_reclaim_tx_buffer(spw_port_t* port,
                                        spw_timeout_us_t timeout_us,
                                        spw_buffer_t** out_buffer) {
    if (validate_port(port) != SPW_OK || out_buffer == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_buffer = nullptr;
    return port->backend->reclaim_tx_buffer(timeout_us, *out_buffer);
}

spw_result_t spw_port_release_tx_buffer(spw_port_t* port,
                                        spw_buffer_t** inout_buffer) {
    if (validate_port(port) != SPW_OK || inout_buffer == nullptr ||
        *inout_buffer == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    const spw_result_t result = port->backend->release_tx_buffer(**inout_buffer);
    if (result == SPW_OK) {
        *inout_buffer = nullptr;
    }
    return result;
}

spw_result_t spw_port_acquire_rx_buffer(spw_port_t* port,
                                        spw_timeout_us_t timeout_us,
                                        spw_buffer_t** out_buffer) {
    if (validate_port(port) != SPW_OK || out_buffer == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_buffer = nullptr;
    return port->backend->acquire_rx_buffer(timeout_us, *out_buffer);
}

spw_result_t spw_port_release_rx_buffer(spw_port_t* port,
                                        spw_buffer_t** inout_buffer) {
    if (validate_port(port) != SPW_OK || inout_buffer == nullptr ||
        *inout_buffer == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    const spw_result_t result = port->backend->release_rx_buffer(**inout_buffer);
    if (result == SPW_OK) {
        *inout_buffer = nullptr;
    }
    return result;
}

spw_result_t spw_port_send_time_code(spw_port_t* port,
                                     const spw_time_code_t* time_code,
                                     spw_timeout_us_t timeout_us) {
    if (validate_port(port) != SPW_OK || time_code == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->backend->send_time_code(*time_code, timeout_us);
}

spw_result_t spw_port_receive_time_code(spw_port_t* port,
                                        spw_time_code_t* time_code,
                                        spw_timeout_us_t timeout_us) {
    if (validate_port(port) != SPW_OK || time_code == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->backend->receive_time_code(*time_code, timeout_us);
}

spw_result_t spw_port_get_statistics(const spw_port_t* port,
                                     spw_statistics_t* out_statistics) {
    if (validate_port(port) != SPW_OK || out_statistics == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return port->backend->get_statistics(*out_statistics);
}

spw_result_t spw_port_clear_statistics(spw_port_t* port) {
    return validate_port(port) == SPW_OK ? port->backend->clear_statistics()
                                         : SPW_ERR_INVALID_ARGUMENT;
}

} // extern "C"
