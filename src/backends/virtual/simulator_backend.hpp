// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/backend.hpp"
#include "core/buffer.hpp"

#include <spwkit/simulator.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace spwkit::detail {

struct VirtualLink;

/**
 * Process-local two-peer SpaceWire simulator backend.
 *
 * Instances attach to endpoint A or B of a registry entry identified by
 * spw_simulator_config_t::link_id. Both endpoints are equal peers; there is no
 * server/client role at either the public API or backend-contract boundary.
 */
class SimulatorBackend final : public Backend {
public:
    static constexpr std::size_t max_packet_size = 4096;
    static constexpr std::size_t packet_queue_depth = 8;
    static constexpr std::size_t time_code_queue_depth = 8;
    static constexpr std::size_t max_local_links = 16;

    explicit SimulatorBackend(const spw_simulator_config_t& config) noexcept;
    ~SimulatorBackend() override;

    SimulatorBackend(const SimulatorBackend&) = delete;
    SimulatorBackend& operator=(const SimulatorBackend&) = delete;

    spw_result_t attach() noexcept;

    spw_result_t start() noexcept override;
    spw_result_t stop() noexcept override;
    spw_result_t reset() noexcept override;

    spw_result_t get_link_state(spw_link_state_t& state) const noexcept override;
    spw_result_t get_capabilities(spw_capabilities_t& capabilities) const noexcept override;

    spw_result_t send(const spw_packet_t& packet,
                      spw_timeout_us_t timeout_us) noexcept override;
    spw_result_t receive(spw_packet_t& packet,
                         spw_timeout_us_t timeout_us) noexcept override;

    spw_result_t send_time_code(const spw_time_code_t& time_code,
                                spw_timeout_us_t timeout_us) noexcept override;
    spw_result_t receive_time_code(spw_time_code_t& time_code,
                                   spw_timeout_us_t timeout_us) noexcept override;

    spw_result_t get_statistics(spw_statistics_t& statistics) const noexcept override;
    spw_result_t clear_statistics() noexcept override;

    spw_result_t acquire_tx_buffer(std::size_t min_capacity,
                                   spw_timeout_us_t timeout_us,
                                   spw_buffer_t*& out_buffer) noexcept override;
    spw_result_t submit_tx_buffer(spw_buffer_t& buffer,
                                  spw_timeout_us_t timeout_us) noexcept override;
    spw_result_t reclaim_tx_buffer(spw_timeout_us_t timeout_us,
                                   spw_buffer_t*& out_buffer) noexcept override;
    spw_result_t release_tx_buffer(spw_buffer_t& buffer) noexcept override;
    spw_result_t acquire_rx_buffer(spw_timeout_us_t timeout_us,
                                   spw_buffer_t*& out_buffer) noexcept override;
    spw_result_t release_rx_buffer(spw_buffer_t& buffer) noexcept override;

private:
    struct TxBufferSlot {
        std::array<std::uint8_t, max_packet_size> storage{};
        spw_buffer descriptor{};
    };

    void detach() noexcept;
    void initialize_zero_copy_buffers() noexcept;
    void reset_zero_copy_buffers() noexcept;

    static bool valid_terminator(spw_terminator_t terminator) noexcept;
    static bool valid_time_code(const spw_time_code_t& time_code) noexcept;

    std::uint64_t link_id_{0};
    std::size_t endpoint_index_{0};
    VirtualLink* link_{nullptr};
    std::array<TxBufferSlot, packet_queue_depth> tx_buffers_{};
    spw_buffer rx_buffer_{};
    bool rx_buffer_acquired_{false};
};

} // namespace spwkit::detail
