// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "core/backend.hpp"

#include <spwkit/udp.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace spwkit::detail {

class UdpBackend final : public Backend {
public:
    static constexpr std::size_t max_packet_size = 1024u * 1024u;
    static constexpr std::size_t time_code_queue_depth = 8u;

    explicit UdpBackend(const spw_udp_config_t& config) noexcept;
    ~UdpBackend() override;

    UdpBackend(const UdpBackend&) = delete;
    UdpBackend& operator=(const UdpBackend&) = delete;

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
    spw_result_t wait_readable(spw_timeout_us_t timeout_us) noexcept;
    spw_result_t pump_one(spw_timeout_us_t timeout_us) noexcept;
    spw_result_t process_data(const std::uint8_t* datagram,
                              std::size_t datagram_size) noexcept;
    spw_result_t send_datagram(const std::uint8_t* bytes,
                               std::size_t size,
                               spw_timeout_us_t timeout_us) noexcept;
    void clear_reassembly() noexcept;
    void close_socket() noexcept;

    spw_udp_config_t config_{};
    int socket_fd_{-1};
    spw_link_state_t state_{SPW_LINK_ERROR_RESET};
    spw_statistics_t statistics_{};
    std::uint32_t next_sequence_{1u};
    std::uint32_t next_message_id_{1u};

    std::array<std::uint8_t, 65507u> datagram_{};
    std::array<std::uint8_t, max_packet_size> reassembly_{};
    std::uint32_t reassembly_message_id_{0u};
    std::uint32_t reassembly_total_size_{0u};
    std::uint32_t reassembly_received_{0u};
    spw_terminator_t reassembly_terminator_{SPW_TERMINATOR_EOP};
    bool reassembly_active_{false};

    std::array<std::uint8_t, max_packet_size> pending_packet_{};
    std::size_t pending_packet_size_{0u};
    spw_terminator_t pending_packet_terminator_{SPW_TERMINATOR_EOP};
    bool pending_packet_valid_{false};

    std::array<spw_time_code_t, time_code_queue_depth> time_codes_{};
    std::size_t time_code_head_{0u};
    std::size_t time_code_count_{0u};
};

} // namespace spwkit::detail
