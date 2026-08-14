// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <spwkit/types.h>

#include <cstddef>

namespace spwkit::detail {

/**
 * Internal backend contract used by libspwkit.
 *
 * Backends implement SpaceWire-facing behavior while keeping transport,
 * operating-system, simulator, DMA, and vendor-specific details below this
 * boundary. Applications never interact with Backend directly.
 */
class Backend {
public:
    virtual ~Backend() = default;

    virtual spw_result_t start() noexcept = 0;
    virtual spw_result_t stop() noexcept = 0;
    virtual spw_result_t reset() noexcept = 0;

    virtual spw_result_t get_link_state(spw_link_state_t& state) const noexcept = 0;
    virtual spw_result_t get_capabilities(spw_capabilities_t& capabilities) const noexcept = 0;
    virtual bool supports_zero_copy() const noexcept { return false; }

    virtual spw_result_t send(const spw_packet_t& packet,
                              spw_timeout_us_t timeout_us) noexcept = 0;
    virtual spw_result_t receive(spw_packet_t& packet,
                                 spw_timeout_us_t timeout_us) noexcept = 0;

    virtual spw_result_t send_time_code(const spw_time_code_t& time_code,
                                        spw_timeout_us_t timeout_us) noexcept = 0;
    virtual spw_result_t receive_time_code(spw_time_code_t& time_code,
                                           spw_timeout_us_t timeout_us) noexcept = 0;

    virtual spw_result_t get_statistics(spw_statistics_t& statistics) const noexcept = 0;
    virtual spw_result_t clear_statistics() noexcept = 0;

    /* Optional ownership-oriented packet path. */
    virtual spw_result_t acquire_tx_buffer(std::size_t,
                                           spw_timeout_us_t,
                                           spw_buffer_t*& out_buffer) noexcept {
        out_buffer = nullptr;
        return SPW_ERR_UNSUPPORTED;
    }

    virtual spw_result_t submit_tx_buffer(spw_buffer_t&,
                                          spw_timeout_us_t) noexcept {
        return SPW_ERR_UNSUPPORTED;
    }

    virtual spw_result_t reclaim_tx_buffer(spw_timeout_us_t,
                                           spw_buffer_t*& out_buffer) noexcept {
        out_buffer = nullptr;
        return SPW_ERR_UNSUPPORTED;
    }

    virtual spw_result_t release_tx_buffer(spw_buffer_t&) noexcept {
        return SPW_ERR_UNSUPPORTED;
    }

    virtual spw_result_t acquire_rx_buffer(spw_timeout_us_t,
                                           spw_buffer_t*& out_buffer) noexcept {
        out_buffer = nullptr;
        return SPW_ERR_UNSUPPORTED;
    }

    virtual spw_result_t release_rx_buffer(spw_buffer_t&) noexcept {
        return SPW_ERR_UNSUPPORTED;
    }
};

} // namespace spwkit::detail
