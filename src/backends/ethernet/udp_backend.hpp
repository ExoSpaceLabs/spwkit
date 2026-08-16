// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "backends/ethernet/deterministic_faults.hpp"
#include "backends/ethernet/fragment_reassembler.hpp"
#include "backends/ethernet/virtual_link_timing.hpp"
#include "backends/ethernet/vspw_tp.hpp"
#include "core/backend.hpp"

#include <spwkit/udp.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace spwkit::detail {

class UdpBackend final : public Backend {
public:
    static constexpr std::size_t max_packet_size = 1024u * 1024u;
    static constexpr std::size_t time_code_queue_depth = 8u;
    static constexpr std::size_t recent_message_depth = 32u;
    static constexpr std::size_t retired_session_depth = 8u;

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
    spw_result_t get_fault_statistics(
        spw_fault_statistics_t& statistics) const noexcept override;
    spw_result_t clear_fault_statistics() noexcept override;

private:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using MessageType = spwkit::ethernet::vspw_tp::MessageType;
    using Header = spwkit::ethernet::vspw_tp::Header;
    using FragmentReassembler = spwkit::ethernet::FragmentReassembler<max_packet_size>;
    using FaultInjector = spwkit::ethernet::DeterministicFaultInjector;
    using VirtualLinkTiming = spwkit::ethernet::VirtualLinkTiming;
    using VirtualLinkEvent = spwkit::ethernet::VirtualLinkEvent;

    enum class PendingTxKind : std::uint8_t {
        None = 0u,
        Data,
        TimeCode,
    };

    struct DeliveredKey {
        MessageType type{MessageType::Data};
        std::uint32_t message_id{0u};
    };

    spw_result_t wait_readable(spw_timeout_us_t timeout_us) noexcept;
    spw_result_t pump_one(spw_timeout_us_t timeout_us) noexcept;
    spw_result_t process_data(const Header& header,
                              const std::uint8_t* payload) noexcept;
    spw_result_t process_time_code(const Header& header,
                                   const std::uint8_t* payload) noexcept;
    spw_result_t process_keepalive(const Header& header) noexcept;
    spw_result_t process_ack(const Header& header,
                             const std::uint8_t* payload) noexcept;

    spw_result_t send_datagram(const std::uint8_t* bytes,
                               std::size_t size,
                               spw_timeout_us_t timeout_us) noexcept;
    spw_result_t send_datagram_raw(const std::uint8_t* bytes,
                                   std::size_t size,
                                   spw_timeout_us_t timeout_us) noexcept;
    spw_result_t wait_transport_fault_delay(std::uint32_t delay_us,
                                            spw_timeout_us_t timeout_us) noexcept;
    spw_result_t wait_virtual_link_delay(std::uint64_t delay_us,
                                         spw_timeout_us_t timeout_us) noexcept;
    spw_result_t send_ack(std::uint32_t message_id) noexcept;
    spw_result_t send_keepalive(spw_timeout_us_t timeout_us) noexcept;
    spw_result_t transmit_pending(spw_timeout_us_t timeout_us) noexcept;
    spw_result_t service_pending_tx() noexcept;
    spw_result_t ensure_peer(spw_timeout_us_t timeout_us) noexcept;
    spw_result_t wait_for_tx_slot(spw_timeout_us_t timeout_us) noexcept;

    void maybe_send_keepalive() noexcept;
    void refresh_peer_state() noexcept;
    void note_peer_activity() noexcept;
    bool reset_remote_session(std::uint64_t session_id) noexcept;
    void remember_retired_session(std::uint64_t session_id) noexcept;
    void mark_peer_lost() noexcept;
    void clear_reassembly() noexcept;
    void expire_reassembly() noexcept;
    void clear_pending_tx() noexcept;
    void clear_recent_messages() noexcept;
    void clear_retired_sessions() noexcept;
    void clear_reordered_datagram() noexcept;
    void close_socket() noexcept;

    bool peer_is_current() const noexcept;
    bool is_retired_session(std::uint64_t session_id) const noexcept;
    bool recently_delivered(MessageType type, std::uint32_t message_id) const noexcept;
    void remember_delivered(MessageType type, std::uint32_t message_id) noexcept;

    spw_udp_config_t config_{};
    VirtualLinkTiming virtual_timing_{};
    FaultInjector fault_injector_;
    int socket_fd_{-1};
    spw_link_state_t state_{SPW_LINK_ERROR_RESET};
    spw_statistics_t statistics_{};
    spw_fault_statistics_t fault_statistics_{};
    std::uint32_t next_sequence_{1u};
    std::uint32_t next_message_id_{1u};

    std::array<std::uint8_t, 65507u> tx_datagram_{};
    std::array<std::uint8_t, 65507u> rx_datagram_{};
    std::array<std::uint8_t, 64u> control_datagram_{};
    std::array<std::uint8_t, 65507u> reordered_datagram_{};
    std::size_t reordered_datagram_size_{0u};
    bool reordered_datagram_valid_{false};

    FragmentReassembler reassembly_{};
    TimePoint reassembly_last_fragment_{};

    std::array<std::uint8_t, max_packet_size> pending_packet_{};
    std::size_t pending_packet_size_{0u};
    spw_terminator_t pending_packet_terminator_{SPW_TERMINATOR_EOP};
    bool pending_packet_valid_{false};

    std::array<spw_time_code_t, time_code_queue_depth> time_codes_{};
    std::size_t time_code_head_{0u};
    std::size_t time_code_count_{0u};

    std::array<std::uint8_t, max_packet_size> pending_tx_packet_{};
    std::size_t pending_tx_packet_size_{0u};
    spw_terminator_t pending_tx_terminator_{SPW_TERMINATOR_EOP};
    spw_time_code_t pending_tx_time_code_{};
    PendingTxKind pending_tx_kind_{PendingTxKind::None};
    std::uint32_t pending_tx_message_id_{0u};
    std::uint16_t pending_tx_retries_{0u};
    bool pending_tx_failed_{false};
    TimePoint pending_tx_last_send_{};

    std::array<DeliveredKey, recent_message_depth> recent_messages_{};
    std::size_t recent_message_head_{0u};
    std::size_t recent_message_count_{0u};

    std::array<std::uint64_t, retired_session_depth> retired_sessions_{};
    std::size_t retired_session_head_{0u};
    std::size_t retired_session_count_{0u};

    std::uint64_t local_session_id_{0u};
    std::uint64_t remote_session_id_{0u};
    bool peer_seen_{false};
    TimePoint last_peer_rx_{};
    TimePoint last_keepalive_tx_{};
};

} // namespace spwkit::detail
