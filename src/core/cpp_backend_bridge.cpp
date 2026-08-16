// SPDX-License-Identifier: Apache-2.0

/*
 * Temporary adapter for the v0.2 C++ UDP backend. The C11 core dispatches
 * exclusively through backend_c.h; #48 removes this file when the distributed
 * runtime moves to C as well.
 */

#include "core/backend_c.h"
#include "backends/ethernet/udp_backend.hpp"

#include <spwkit/udp.h>

#include <new>

namespace {
using Udp = spwkit::detail::UdpBackend;

template <typename BackendType>
BackendType* backend(void* context) noexcept {
    return static_cast<BackendType*>(context);
}

template <typename BackendType>
const BackendType* backend(const void* context) noexcept {
    return static_cast<const BackendType*>(context);
}

spw_result_t op_start(void* context) noexcept { return backend<Udp>(context)->start(); }
spw_result_t op_stop(void* context) noexcept { return backend<Udp>(context)->stop(); }
spw_result_t op_reset(void* context) noexcept { return backend<Udp>(context)->reset(); }

spw_result_t op_get_link_state(const void* context,
                               spw_link_state_t* out_state) noexcept {
    return backend<Udp>(context)->get_link_state(*out_state);
}

spw_result_t op_get_capabilities(const void* context,
                                  spw_capabilities_t* out_capabilities) noexcept {
    return backend<Udp>(context)->get_capabilities(*out_capabilities);
}

bool op_supports_zero_copy(const void* context) noexcept {
    return backend<Udp>(context)->supports_zero_copy();
}

spw_result_t op_send(void* context,
                     const spw_packet_t* packet,
                     spw_timeout_us_t timeout_us) noexcept {
    return backend<Udp>(context)->send(*packet, timeout_us);
}

spw_result_t op_receive(void* context,
                        spw_packet_t* packet,
                        spw_timeout_us_t timeout_us) noexcept {
    return backend<Udp>(context)->receive(*packet, timeout_us);
}

spw_result_t op_send_time_code(void* context,
                               const spw_time_code_t* time_code,
                               spw_timeout_us_t timeout_us) noexcept {
    return backend<Udp>(context)->send_time_code(*time_code, timeout_us);
}

spw_result_t op_receive_time_code(void* context,
                                  spw_time_code_t* time_code,
                                  spw_timeout_us_t timeout_us) noexcept {
    return backend<Udp>(context)->receive_time_code(*time_code, timeout_us);
}

spw_result_t op_get_statistics(const void* context,
                               spw_statistics_t* out_statistics) noexcept {
    return backend<Udp>(context)->get_statistics(*out_statistics);
}

spw_result_t op_clear_statistics(void* context) noexcept {
    return backend<Udp>(context)->clear_statistics();
}

spw_result_t op_get_fault_statistics(
    const void* context,
    spw_fault_statistics_t* out_statistics) noexcept {
    return backend<Udp>(context)->get_fault_statistics(*out_statistics);
}

spw_result_t op_clear_fault_statistics(void* context) noexcept {
    return backend<Udp>(context)->clear_fault_statistics();
}

spw_result_t op_acquire_tx_buffer(void* context,
                                  size_t min_capacity,
                                  spw_timeout_us_t timeout_us,
                                  spw_buffer_t** out_buffer) noexcept {
    spw_buffer_t* value = nullptr;
    const spw_result_t result = backend<Udp>(context)->acquire_tx_buffer(
        min_capacity, timeout_us, value);
    *out_buffer = value;
    return result;
}

spw_result_t op_submit_tx_buffer(void* context,
                                 spw_buffer_t* buffer_value,
                                 spw_timeout_us_t timeout_us) noexcept {
    return backend<Udp>(context)->submit_tx_buffer(*buffer_value, timeout_us);
}

spw_result_t op_reclaim_tx_buffer(void* context,
                                  spw_timeout_us_t timeout_us,
                                  spw_buffer_t** out_buffer) noexcept {
    spw_buffer_t* value = nullptr;
    const spw_result_t result = backend<Udp>(context)->reclaim_tx_buffer(
        timeout_us, value);
    *out_buffer = value;
    return result;
}

spw_result_t op_release_tx_buffer(void* context,
                                  spw_buffer_t* buffer_value) noexcept {
    return backend<Udp>(context)->release_tx_buffer(*buffer_value);
}

spw_result_t op_acquire_rx_buffer(void* context,
                                  spw_timeout_us_t timeout_us,
                                  spw_buffer_t** out_buffer) noexcept {
    spw_buffer_t* value = nullptr;
    const spw_result_t result = backend<Udp>(context)->acquire_rx_buffer(
        timeout_us, value);
    *out_buffer = value;
    return result;
}

spw_result_t op_release_rx_buffer(void* context,
                                  spw_buffer_t* buffer_value) noexcept {
    return backend<Udp>(context)->release_rx_buffer(*buffer_value);
}

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

const spw_backend_ops_t UDP_OPS = {
    op_start,
    op_stop,
    op_reset,
    op_get_link_state,
    op_get_capabilities,
    op_supports_zero_copy,
    op_send,
    op_receive,
    op_send_time_code,
    op_receive_time_code,
    op_get_statistics,
    op_clear_statistics,
    op_get_fault_statistics,
    op_clear_fault_statistics,
    op_acquire_tx_buffer,
    op_submit_tx_buffer,
    op_reclaim_tx_buffer,
    op_release_tx_buffer,
    op_acquire_rx_buffer,
    op_release_rx_buffer,
};
} // namespace

extern "C" const spw_backend_factory_t* spw_cpp_udp_backend_factory(void) {
    static const spw_backend_factory_t factory = {
        sizeof(Udp),
        alignof(Udp),
        construct_udp,
        destroy_udp,
        &UDP_OPS,
    };
    return &factory;
}
