// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/backend.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace spwkit::detail {

class LoopbackBackend final : public Backend {
public:
    static constexpr std::size_t max_packet_size = 4096;
    static constexpr std::size_t packet_queue_depth = 8;
    static constexpr std::size_t time_code_queue_depth = 8;

    LoopbackBackend() noexcept = default;

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
    struct PacketSlot {
        std::array<std::uint8_t, max_packet_size> data{};
        std::size_t length{0};
        spw_terminator_t terminator{SPW_TERMINATOR_EOP};
    };

    struct PacketQueue {
        std::array<PacketSlot, packet_queue_depth> slots{};
        std::size_t head{0};
        std::size_t tail{0};
        std::size_t count{0};
    };

    struct TimeCodeQueue {
        std::array<spw_time_code_t, time_code_queue_depth> slots{};
        std::size_t head{0};
        std::size_t tail{0};
        std::size_t count{0};
    };

    static bool valid_terminator(spw_terminator_t terminator) noexcept;
    static bool valid_time_code(const spw_time_code_t& time_code) noexcept;

    void clear_queues() noexcept;

    spw_link_state_t state_{SPW_LINK_ERROR_RESET};
    PacketQueue packets_{};
    TimeCodeQueue time_codes_{};
    spw_statistics_t statistics_{};
};

} // namespace spwkit::detail
