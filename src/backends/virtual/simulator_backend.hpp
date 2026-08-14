// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/backend.hpp"

#include <spwkit/simulator.h>

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

private:
    void detach() noexcept;

    static bool valid_terminator(spw_terminator_t terminator) noexcept;
    static bool valid_time_code(const spw_time_code_t& time_code) noexcept;

    std::uint64_t link_id_{0};
    std::size_t endpoint_index_{0};
    VirtualLink* link_{nullptr};
};

} // namespace spwkit::detail
