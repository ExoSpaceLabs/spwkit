// SPDX-License-Identifier: Apache-2.0

/*
 * Temporary adapter used only while the simulator and UDP implementations are
 * still C++. The C11 core dispatches exclusively through backend_c.h. These
 * adapters disappear as #47/#48 move the remaining runtime below the same C
 * backend contract.
 */

#include "core/backend_c.h"

#ifdef SPWKIT_HAS_SIMULATOR
#include "backends/virtual/simulator_backend.hpp"
#include <spwkit/simulator.h>
#endif
#ifdef SPWKIT_HAS_UDP
#include "backends/ethernet/udp_backend.hpp"
#include <spwkit/udp.h>
#endif

#include <new>

template <typename BackendType>
static BackendType* backend(void* context) noexcept {
    return static_cast<BackendType*>(context);
}

template <typename BackendType>
static const BackendType* backend(const void* context) noexcept {
    return static_cast<const BackendType*>(context);
}

template <typename BackendType>
static spw_result_t op_start(void* context) noexcept {
    return backend<BackendType>(context)->start();
}

template <typename BackendType>
static spw_result_t op_stop(void* context) noexcept {
    return backend<BackendType>(context)->stop();
}

template <typename BackendType>
static spw_result_t op_reset(void* context) noexcept {
    return backend<BackendType>(context)->reset();
}

template <typename BackendType>
static spw_result_t op_get_link_state(const void* context,
                                      spw_link_state_t* out_state) noexcept {
    return backend<BackendType>(context)->get_link_state(*out_state);
}

template <typename BackendType>
static spw_result_t op_get_capabilities(
    const void* context,
    spw_capabilities_t* out_capabilities) noexcept {
    return backend<BackendType>(context)->get_capabilities(*out_capabilities);
}

template <typename BackendType>
static bool op_supports_zero_copy(const void* context) noexcept {
    return backend<BackendType>(context)->supports_zero_copy();
}

template <typename BackendType>
static spw_result_t op_send(void* context,
                            const spw_packet_t* packet,
                            spw_timeout_us_t timeout_us) noexcept {
    return backend<BackendType>(context)->send(*packet, timeout_us);
}

template <typename BackendType>
static spw_result_t op_receive(void* context,
                               spw_packet_t* packet,
                               spw_timeout_us_t timeout_us) noexcept {
    return backend<BackendType>(context)->receive(*packet, timeout_us);
}

template <typename BackendType>
static spw_result_t op_send_time_code(void* context,
                                      const spw_time_code_t* time_code,
                                      spw_timeout_us_t timeout_us) noexcept {
    return backend<BackendType>(context)->send_time_code(*time_code, timeout_us);
}

template <typename BackendType>
static spw_result_t op_receive_time_code(void* context,
                                         spw_time_code_t* time_code,
                                         spw_timeout_us_t timeout_us) noexcept {
    return backend<BackendType>(context)->receive_time_code(*time_code, timeout_us);
}

template <typename BackendType>
static spw_result_t op_get_statistics(
    const void* context,
    spw_statistics_t* out_statistics) noexcept {
    return backend<BackendType>(context)->get_statistics(*out_statistics);
}

template <typename BackendType>
static spw_result_t op_clear_statistics(void* context) noexcept {
    return backend<BackendType>(context)->clear_statistics();
}

template <typename BackendType>
static spw_result_t op_get_fault_statistics(
    const void* context,
    spw_fault_statistics_t* out_statistics) noexcept {
    return backend<BackendType>(context)->get_fault_statistics(*out_statistics);
}

template <typename BackendType>
static spw_result_t op_clear_fault_statistics(void* context) noexcept {
    return backend<BackendType>(context)->clear_fault_statistics();
}

template <typename BackendType>
static spw_result_t op_acquire_tx_buffer(void* context,
                                         size_t min_capacity,
                                         spw_timeout_us_t timeout_us,
                                         spw_buffer_t** out_buffer) noexcept {
    spw_buffer_t* value = nullptr;
    const spw_result_t result = backend<BackendType>(context)->acquire_tx_buffer(
        min_capacity, timeout_us, value);
    *out_buffer = value;
    return result;
}

template <typename BackendType>
static spw_result_t op_submit_tx_buffer(void* context,
                                        spw_buffer_t* buffer_value,
                                        spw_timeout_us_t timeout_us) noexcept {
    return backend<BackendType>(context)->submit_tx_buffer(*buffer_value, timeout_us);
}

template <typename BackendType>
static spw_result_t op_reclaim_tx_buffer(void* context,
                                         spw_timeout_us_t timeout_us,
                                         spw_buffer_t** out_buffer) noexcept {
    spw_buffer_t* value = nullptr;
    const spw_result_t result = backend<BackendType>(context)->reclaim_tx_buffer(
        timeout_us, value);
    *out_buffer = value;
    return result;
}

template <typename BackendType>
static spw_result_t op_release_tx_buffer(void* context,
                                         spw_buffer_t* buffer_value) noexcept {
    return backend<BackendType>(context)->release_tx_buffer(*buffer_value);
}

template <typename BackendType>
static spw_result_t op_acquire_rx_buffer(void* context,
                                         spw_timeout_us_t timeout_us,
                                         spw_buffer_t** out_buffer) noexcept {
    spw_buffer_t* value = nullptr;
    const spw_result_t result = backend<BackendType>(context)->acquire_rx_buffer(
        timeout_us, value);
    *out_buffer = value;
    return result;
}

template <typename BackendType>
static spw_result_t op_release_rx_buffer(void* context,
                                         spw_buffer_t* buffer_value) noexcept {
    return backend<BackendType>(context)->release_rx_buffer(*buffer_value);
}

template <typename BackendType>
static const spw_backend_ops_t* ops() noexcept {
    static const spw_backend_ops_t value = {
        &op_start<BackendType>,
        &op_stop<BackendType>,
        &op_reset<BackendType>,
        &op_get_link_state<BackendType>,
        &op_get_capabilities<BackendType>,
        &op_supports_zero_copy<BackendType>,
        &op_send<BackendType>,
        &op_receive<BackendType>,
        &op_send_time_code<BackendType>,
        &op_receive_time_code<BackendType>,
        &op_get_statistics<BackendType>,
        &op_clear_statistics<BackendType>,
        &op_get_fault_statistics<BackendType>,
        &op_clear_fault_statistics<BackendType>,
        &op_acquire_tx_buffer<BackendType>,
        &op_submit_tx_buffer<BackendType>,
        &op_reclaim_tx_buffer<BackendType>,
        &op_release_tx_buffer<BackendType>,
        &op_acquire_rx_buffer<BackendType>,
        &op_release_rx_buffer<BackendType>,
    };
    return &value;
}

#ifdef SPWKIT_HAS_SIMULATOR
namespace {
using Simulator = spwkit::detail::SimulatorBackend;

spw_result_t construct_simulator(void* context,
                                 const spw_port_config_t* config) noexcept {
    const auto* simulator_config =
        static_cast<const spw_simulator_config_t*>(config->backend_config);
    auto* instance = ::new (context) Simulator(*simulator_config);
    const spw_result_t result = instance->attach();
    if (result != SPW_OK) {
        instance->~Simulator();
    }
    return result;
}

void destroy_simulator(void* context) noexcept {
    backend<Simulator>(context)->~Simulator();
}
} // namespace

extern "C" const spw_backend_factory_t* spw_cpp_simulator_backend_factory(void) {
    static const spw_backend_factory_t factory = {
        sizeof(Simulator),
        alignof(Simulator),
        &construct_simulator,
        &destroy_simulator,
        ops<Simulator>(),
    };
    return &factory;
}
#endif

#ifdef SPWKIT_HAS_UDP
namespace {
using Udp = spwkit::detail::UdpBackend;

spw_result_t construct_udp(void* context,
                           const spw_port_config_t* config) noexcept {
    const auto* udp_config =
        static_cast<const spw_udp_config_t*>(config->backend_config);
    auto* instance = ::new (context) Udp(*udp_config);
    const spw_result_t result = instance->attach();
    if (result != SPW_OK) {
        instance->~Udp();
    }
    return result;
}

void destroy_udp(void* context) noexcept {
    backend<Udp>(context)->~Udp();
}
} // namespace

extern "C" const spw_backend_factory_t* spw_cpp_udp_backend_factory(void) {
    static const spw_backend_factory_t factory = {
        sizeof(Udp),
        alignof(Udp),
        &construct_udp,
        &destroy_udp,
        ops<Udp>(),
    };
    return &factory;
}
#endif
