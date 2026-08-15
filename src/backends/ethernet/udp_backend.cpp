// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/udp_backend.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace spwkit::detail {
namespace {

using namespace spwkit::ethernet::vspw_tp;
using Clock = std::chrono::steady_clock;

bool valid_terminator(spw_terminator_t terminator) noexcept {
    return terminator == SPW_TERMINATOR_EOP || terminator == SPW_TERMINATOR_EEP;
}

bool valid_time_code(const spw_time_code_t& time_code) noexcept {
    return time_code.time_count <= 63u && time_code.control_flags <= 3u;
}

int timeout_ms(spw_timeout_us_t timeout_us) noexcept {
    if (timeout_us == SPW_TIMEOUT_INFINITE) {
        return -1;
    }
    const std::uint64_t rounded = (timeout_us + 999u) / 1000u;
    return rounded > static_cast<std::uint64_t>(INT_MAX)
               ? INT_MAX
               : static_cast<int>(rounded);
}

spw_timeout_us_t min_timeout(spw_timeout_us_t lhs, spw_timeout_us_t rhs) noexcept {
    if (lhs == SPW_TIMEOUT_INFINITE) {
        return rhs;
    }
    if (rhs == SPW_TIMEOUT_INFINITE) {
        return lhs;
    }
    return std::min(lhs, rhs);
}

std::uint8_t terminator_flag(spw_terminator_t terminator) noexcept {
    return terminator == SPW_TERMINATOR_EEP ? FlagEep : FlagEop;
}

std::uint32_t take_nonzero(std::uint32_t& counter) noexcept {
    std::uint32_t value = counter++;
    if (value == 0u) {
        value = counter++;
    }
    return value;
}

std::uint64_t make_session_id(const void* object,
                              const spw_udp_config_t& config) noexcept {
    const auto ticks = static_cast<std::uint64_t>(
        Clock::now().time_since_epoch().count());
    std::uint64_t value = ticks ^
        (static_cast<std::uint64_t>(static_cast<unsigned>(::getpid())) << 32u) ^
        (static_cast<std::uint64_t>(config.local_port) << 16u) ^
        static_cast<std::uint64_t>(config.link_id) ^
        static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(object));
    return value == 0u ? 1u : value;
}

bool source_matches(const spw_udp_config_t& config,
                    const sockaddr_in& source) noexcept {
    if (source.sin_family != AF_INET || source.sin_port != htons(config.remote_port)) {
        return false;
    }
    in_addr expected{};
    return ::inet_pton(AF_INET, config.remote_address, &expected) == 1 &&
           source.sin_addr.s_addr == expected.s_addr;
}

class Deadline {
public:
    explicit Deadline(spw_timeout_us_t timeout_us) noexcept
        : infinite_(timeout_us == SPW_TIMEOUT_INFINITE),
          end_(infinite_ ? Clock::time_point::max()
                         : Clock::now() + std::chrono::microseconds(timeout_us)) {}

    spw_timeout_us_t remaining() const noexcept {
        if (infinite_) {
            return SPW_TIMEOUT_INFINITE;
        }
        const auto now = Clock::now();
        if (now >= end_) {
            return SPW_TIMEOUT_IMMEDIATE;
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::microseconds>(end_ - now).count();
        return remaining <= 0 ? SPW_TIMEOUT_IMMEDIATE
                              : static_cast<spw_timeout_us_t>(remaining);
    }

    bool expired() const noexcept {
        return !infinite_ && Clock::now() >= end_;
    }

private:
    bool infinite_{false};
    Clock::time_point end_{};
};

} // namespace

UdpBackend::UdpBackend(const spw_udp_config_t& config) noexcept : config_(config) {}

UdpBackend::~UdpBackend() {
    close_socket();
}

spw_result_t UdpBackend::attach() noexcept {
    if (config_.version != SPW_UDP_CONFIG_VERSION ||
        config_.struct_size < sizeof(spw_udp_config_t) ||
        config_.remote_port == 0u || config_.link_id == 0u ||
        config_.fragment_payload_size < 256u ||
        config_.fragment_payload_size > kMaxFragmentPayload ||
        config_.max_retries == 0u || config_.ack_timeout_ms == 0u ||
        config_.keepalive_interval_ms == 0u ||
        config_.peer_timeout_ms <= config_.keepalive_interval_ms ||
        config_.reserved != 0u) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(config_.local_port);
    if (::inet_pton(AF_INET, config_.local_address, &local.sin_addr) != 1) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    in_addr remote{};
    if (::inet_pton(AF_INET, config_.remote_address, &remote) != 1) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    socket_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        return SPW_ERR_BACKEND;
    }

    int reuse = 1;
    (void)::setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    if (::bind(socket_fd_, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        close_socket();
        return SPW_ERR_BACKEND;
    }

    state_ = SPW_LINK_READY;
    return SPW_OK;
}

void UdpBackend::close_socket() noexcept {
    if (socket_fd_ >= 0) {
        (void)::close(socket_fd_);
        socket_fd_ = -1;
    }
}

void UdpBackend::clear_reassembly() noexcept {
    reassembly_message_id_ = 0u;
    reassembly_total_size_ = 0u;
    reassembly_received_ = 0u;
    reassembly_terminator_ = SPW_TERMINATOR_EOP;
    reassembly_active_ = false;
}

void UdpBackend::clear_pending_tx() noexcept {
    pending_tx_packet_size_ = 0u;
    pending_tx_terminator_ = SPW_TERMINATOR_EOP;
    pending_tx_time_code_ = {};
    pending_tx_kind_ = PendingTxKind::None;
    pending_tx_message_id_ = 0u;
    pending_tx_retries_ = 0u;
    pending_tx_failed_ = false;
    pending_tx_last_send_ = {};
}

void UdpBackend::clear_recent_messages() noexcept {
    recent_message_head_ = 0u;
    recent_message_count_ = 0u;
}

spw_result_t UdpBackend::start() noexcept {
    if (socket_fd_ < 0) {
        return SPW_ERR_INVALID_STATE;
    }

    clear_reassembly();
    clear_pending_tx();
    clear_recent_messages();
    pending_packet_valid_ = false;
    time_code_head_ = 0u;
    time_code_count_ = 0u;
    next_sequence_ = 1u;
    next_message_id_ = 1u;
    local_session_id_ = make_session_id(this, config_);
    remote_session_id_ = 0u;
    peer_seen_ = false;
    last_peer_rx_ = {};
    last_keepalive_tx_ = {};
    state_ = SPW_LINK_CONNECTING;

    const spw_result_t keepalive_result = send_keepalive(SPW_TIMEOUT_IMMEDIATE);
    return keepalive_result == SPW_ERR_TIMEOUT ? SPW_OK : keepalive_result;
}

spw_result_t UdpBackend::stop() noexcept {
    if (socket_fd_ < 0) {
        return SPW_ERR_INVALID_STATE;
    }
    state_ = SPW_LINK_READY;
    clear_reassembly();
    clear_pending_tx();
    clear_recent_messages();
    pending_packet_valid_ = false;
    time_code_head_ = 0u;
    time_code_count_ = 0u;
    peer_seen_ = false;
    remote_session_id_ = 0u;
    return SPW_OK;
}

spw_result_t UdpBackend::reset() noexcept {
    if (socket_fd_ < 0) {
        return SPW_ERR_INVALID_STATE;
    }
    state_ = SPW_LINK_ERROR_RESET;
    clear_reassembly();
    clear_pending_tx();
    clear_recent_messages();
    pending_packet_valid_ = false;
    time_code_head_ = 0u;
    time_code_count_ = 0u;
    peer_seen_ = false;
    remote_session_id_ = 0u;
    return SPW_OK;
}

bool UdpBackend::peer_is_current() const noexcept {
    if (!peer_seen_ || last_peer_rx_ == TimePoint{}) {
        return false;
    }
    return Clock::now() - last_peer_rx_ <=
           std::chrono::milliseconds(config_.peer_timeout_ms);
}

void UdpBackend::mark_peer_lost() noexcept {
    if (state_ != SPW_LINK_ERROR_WAIT) {
        statistics_.link_errors++;
    }
    state_ = SPW_LINK_ERROR_WAIT;
}

void UdpBackend::refresh_peer_state() noexcept {
    if (state_ == SPW_LINK_RUN && !peer_is_current()) {
        mark_peer_lost();
    } else if (state_ == SPW_LINK_ERROR_WAIT && peer_is_current()) {
        state_ = SPW_LINK_RUN;
    }
}

void UdpBackend::note_peer_activity() noexcept {
    peer_seen_ = true;
    last_peer_rx_ = Clock::now();
    if (state_ == SPW_LINK_CONNECTING || state_ == SPW_LINK_ERROR_WAIT) {
        state_ = SPW_LINK_RUN;
    }
}

spw_result_t UdpBackend::get_link_state(spw_link_state_t& state) const noexcept {
    auto* self = const_cast<UdpBackend*>(this);
    if (self->state_ == SPW_LINK_CONNECTING || self->state_ == SPW_LINK_RUN ||
        self->state_ == SPW_LINK_ERROR_WAIT) {
        self->maybe_send_keepalive();
        (void)self->pump_one(SPW_TIMEOUT_IMMEDIATE);
        (void)self->service_pending_tx();
        self->refresh_peer_state();
    }
    state = self->state_;
    return SPW_OK;
}

spw_result_t UdpBackend::get_capabilities(spw_capabilities_t& capabilities) const noexcept {
    capabilities.bits = SPW_CAP_EEP | SPW_CAP_TIME_CODE | SPW_CAP_STATISTICS;
    capabilities.max_packet_size = max_packet_size;
    capabilities.tx_queue_depth = 1u;
    capabilities.rx_queue_depth = 1u;
    capabilities.buffer_alignment = alignof(std::max_align_t);
    return SPW_OK;
}

spw_result_t UdpBackend::send_datagram(const std::uint8_t* bytes,
                                       std::size_t size,
                                       spw_timeout_us_t timeout_us) noexcept {
    if (socket_fd_ < 0) {
        return SPW_ERR_INVALID_STATE;
    }

    pollfd descriptor{};
    descriptor.fd = socket_fd_;
    descriptor.events = POLLOUT;
    int ready = 0;
    do {
        ready = ::poll(&descriptor, 1, timeout_ms(timeout_us));
    } while (ready < 0 && errno == EINTR);
    if (ready == 0) {
        return SPW_ERR_TIMEOUT;
    }
    if (ready < 0 || (descriptor.revents & POLLOUT) == 0) {
        return SPW_ERR_BACKEND;
    }

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(config_.remote_port);
    if (::inet_pton(AF_INET, config_.remote_address, &remote.sin_addr) != 1) {
        return SPW_ERR_BACKEND;
    }

    const ssize_t sent = ::sendto(socket_fd_, bytes, size, 0,
                                  reinterpret_cast<const sockaddr*>(&remote),
                                  sizeof(remote));
    return sent == static_cast<ssize_t>(size) ? SPW_OK : SPW_ERR_BACKEND;
}

spw_result_t UdpBackend::send_ack(std::uint32_t message_id) noexcept {
    if (message_id == 0u) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    Header header{};
    header.type = MessageType::Ack;
    header.link_id = config_.link_id;
    header.sequence = take_nonzero(next_sequence_);
    header.message_id = message_id;
    if (!encode_header(header, control_datagram_.data(), control_datagram_.size())) {
        return SPW_ERR_BACKEND;
    }
    return send_datagram(control_datagram_.data(), kHeaderSize, SPW_TIMEOUT_IMMEDIATE);
}

spw_result_t UdpBackend::send_keepalive(spw_timeout_us_t timeout_us) noexcept {
    Header header{};
    header.type = MessageType::Keepalive;
    header.payload_size = static_cast<std::uint16_t>(kKeepalivePayloadSize);
    header.link_id = config_.link_id;
    header.sequence = take_nonzero(next_sequence_);
    header.total_size = static_cast<std::uint32_t>(kKeepalivePayloadSize);
    if (!encode_header(header, control_datagram_.data(), control_datagram_.size()) ||
        !encode_keepalive_payload(local_session_id_,
                                  control_datagram_.data() + kHeaderSize,
                                  kKeepalivePayloadSize)) {
        return SPW_ERR_BACKEND;
    }

    const spw_result_t result = send_datagram(control_datagram_.data(),
                                               kHeaderSize + kKeepalivePayloadSize,
                                               timeout_us);
    if (result == SPW_OK) {
        last_keepalive_tx_ = Clock::now();
    }
    return result;
}

void UdpBackend::maybe_send_keepalive() noexcept {
    if (socket_fd_ < 0 || local_session_id_ == 0u ||
        (state_ != SPW_LINK_CONNECTING && state_ != SPW_LINK_RUN &&
         state_ != SPW_LINK_ERROR_WAIT)) {
        return;
    }

    const auto interval = std::chrono::milliseconds(config_.keepalive_interval_ms);
    if (last_keepalive_tx_ == TimePoint{} || Clock::now() - last_keepalive_tx_ >= interval) {
        (void)send_keepalive(SPW_TIMEOUT_IMMEDIATE);
    }
}

bool UdpBackend::recently_delivered(MessageType type,
                                    std::uint32_t message_id) const noexcept {
    if (message_id == 0u) {
        return false;
    }
    for (std::size_t i = 0u; i < recent_message_count_; ++i) {
        const std::size_t index = (recent_message_head_ + i) % recent_message_depth;
        if (recent_messages_[index].type == type &&
            recent_messages_[index].message_id == message_id) {
            return true;
        }
    }
    return false;
}

void UdpBackend::remember_delivered(MessageType type,
                                    std::uint32_t message_id) noexcept {
    if (message_id == 0u || recently_delivered(type, message_id)) {
        return;
    }
    if (recent_message_count_ < recent_message_depth) {
        const std::size_t index =
            (recent_message_head_ + recent_message_count_) % recent_message_depth;
        recent_messages_[index] = {type, message_id};
        recent_message_count_++;
        return;
    }
    recent_messages_[recent_message_head_] = {type, message_id};
    recent_message_head_ = (recent_message_head_ + 1u) % recent_message_depth;
}

void UdpBackend::reset_remote_session(std::uint64_t session_id) noexcept {
    if (remote_session_id_ == 0u) {
        remote_session_id_ = session_id;
        note_peer_activity();
        return;
    }
    if (remote_session_id_ != session_id) {
        remote_session_id_ = session_id;
        clear_reassembly();
        clear_recent_messages();
        if (pending_tx_kind_ != PendingTxKind::None) {
            pending_tx_retries_ = 0u;
            pending_tx_failed_ = false;
            pending_tx_last_send_ = {};
        }
    }
    note_peer_activity();
}

spw_result_t UdpBackend::transmit_pending(spw_timeout_us_t timeout_us) noexcept {
    if (pending_tx_kind_ == PendingTxKind::None || pending_tx_message_id_ == 0u) {
        return SPW_OK;
    }

    if (pending_tx_kind_ == PendingTxKind::TimeCode) {
        Header header{};
        header.type = MessageType::TimeCode;
        header.flags = FlagAckRequired;
        header.payload_size = static_cast<std::uint16_t>(kTimeCodePayloadSize);
        header.link_id = config_.link_id;
        header.sequence = take_nonzero(next_sequence_);
        header.message_id = pending_tx_message_id_;
        header.total_size = static_cast<std::uint32_t>(kTimeCodePayloadSize);
        if (!encode_header(header, tx_datagram_.data(), tx_datagram_.size())) {
            return SPW_ERR_BACKEND;
        }
        tx_datagram_[kHeaderSize] = pending_tx_time_code_.time_count;
        tx_datagram_[kHeaderSize + 1u] = pending_tx_time_code_.control_flags;
        const spw_result_t result = send_datagram(tx_datagram_.data(),
                                                  kHeaderSize + kTimeCodePayloadSize,
                                                  timeout_us);
        if (result == SPW_OK) {
            pending_tx_last_send_ = Clock::now();
        }
        return result;
    }

    const std::size_t fragment_size = config_.fragment_payload_size;
    const bool fragmented = pending_tx_packet_size_ > fragment_size;
    std::size_t offset = 0u;

    do {
        const std::size_t remaining = pending_tx_packet_size_ - offset;
        const std::size_t payload_size = fragmented
            ? std::min(fragment_size, remaining)
            : remaining;

        Header header{};
        header.type = MessageType::Data;
        header.flags = static_cast<std::uint8_t>(
            terminator_flag(pending_tx_terminator_) | FlagAckRequired);
        if (fragmented && offset == 0u) {
            header.flags |= FlagFragmentStart;
        }
        if (fragmented && offset + payload_size == pending_tx_packet_size_) {
            header.flags |= FlagFragmentEnd;
        }
        header.payload_size = static_cast<std::uint16_t>(payload_size);
        header.link_id = config_.link_id;
        header.sequence = take_nonzero(next_sequence_);
        header.message_id = pending_tx_message_id_;
        header.fragment_offset = static_cast<std::uint32_t>(offset);
        header.total_size = static_cast<std::uint32_t>(pending_tx_packet_size_);

        if (!encode_header(header, tx_datagram_.data(), tx_datagram_.size())) {
            return SPW_ERR_INVALID_PACKET;
        }
        if (payload_size != 0u) {
            std::memcpy(tx_datagram_.data() + kHeaderSize,
                        pending_tx_packet_.data() + offset,
                        payload_size);
        }
        const spw_result_t result = send_datagram(tx_datagram_.data(),
                                                  kHeaderSize + payload_size,
                                                  timeout_us);
        if (result != SPW_OK) {
            return result;
        }
        offset += payload_size;
    } while (offset < pending_tx_packet_size_);

    pending_tx_last_send_ = Clock::now();
    return SPW_OK;
}

spw_result_t UdpBackend::service_pending_tx() noexcept {
    if (pending_tx_kind_ == PendingTxKind::None) {
        return SPW_OK;
    }

    const auto ack_timeout = std::chrono::milliseconds(config_.ack_timeout_ms);
    if (pending_tx_last_send_ != TimePoint{} &&
        Clock::now() - pending_tx_last_send_ < ack_timeout) {
        return SPW_OK;
    }

    if (pending_tx_retries_ >= config_.max_retries) {
        if (!pending_tx_failed_) {
            pending_tx_failed_ = true;
            statistics_.dropped_packets++;
        }
        mark_peer_lost();
        return SPW_ERR_LINK_UNAVAILABLE;
    }

    const spw_result_t result = transmit_pending(SPW_TIMEOUT_IMMEDIATE);
    if (result == SPW_OK) {
        pending_tx_retries_++;
    }
    return result;
}

spw_result_t UdpBackend::wait_readable(spw_timeout_us_t timeout_us) noexcept {
    pollfd descriptor{};
    descriptor.fd = socket_fd_;
    descriptor.events = POLLIN;
    int ready = 0;
    do {
        ready = ::poll(&descriptor, 1, timeout_ms(timeout_us));
    } while (ready < 0 && errno == EINTR);
    if (ready == 0) {
        return SPW_ERR_TIMEOUT;
    }
    if (ready < 0 || (descriptor.revents & POLLIN) == 0) {
        return SPW_ERR_BACKEND;
    }
    return SPW_OK;
}

spw_result_t UdpBackend::process_ack(const Header& header) noexcept {
    if (pending_tx_kind_ != PendingTxKind::None &&
        header.message_id == pending_tx_message_id_) {
        clear_pending_tx();
    }
    return SPW_OK;
}

spw_result_t UdpBackend::process_keepalive(const Header& header,
                                           const std::uint8_t* payload) noexcept {
    std::uint64_t session_id = 0u;
    if (!decode_keepalive_payload(payload, header.payload_size, session_id)) {
        statistics_.dropped_packets++;
        return SPW_OK;
    }
    reset_remote_session(session_id);
    return SPW_OK;
}

spw_result_t UdpBackend::process_time_code(const Header& header,
                                           const std::uint8_t* payload) noexcept {
    const bool ack_required = (header.flags & FlagAckRequired) != 0u;
    if (ack_required && recently_delivered(MessageType::TimeCode, header.message_id)) {
        (void)send_ack(header.message_id);
        return SPW_OK;
    }

    if (time_code_count_ == time_code_queue_depth) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    const spw_time_code_t time_code{payload[0], payload[1]};
    if (!valid_time_code(time_code)) {
        statistics_.dropped_packets++;
        return SPW_OK;
    }

    const std::size_t index = (time_code_head_ + time_code_count_) % time_code_queue_depth;
    time_codes_[index] = time_code;
    time_code_count_++;

    if (ack_required) {
        remember_delivered(MessageType::TimeCode, header.message_id);
        (void)send_ack(header.message_id);
    }
    return SPW_OK;
}

spw_result_t UdpBackend::process_data(const Header& header,
                                      const std::uint8_t* payload) noexcept {
    const bool ack_required = (header.flags & FlagAckRequired) != 0u;
    if (ack_required && recently_delivered(MessageType::Data, header.message_id)) {
        (void)send_ack(header.message_id);
        return SPW_OK;
    }

    const bool fragmented = header.total_size != header.payload_size;
    const spw_terminator_t terminator =
        (header.flags & FlagEep) != 0u ? SPW_TERMINATOR_EEP : SPW_TERMINATOR_EOP;

    if (!fragmented) {
        if (pending_packet_valid_) {
            return SPW_ERR_RESOURCE_EXHAUSTED;
        }
        if (header.payload_size != 0u) {
            std::memcpy(pending_packet_.data(), payload, header.payload_size);
        }
        pending_packet_size_ = header.payload_size;
        pending_packet_terminator_ = terminator;
        pending_packet_valid_ = true;
        if (ack_required) {
            remember_delivered(MessageType::Data, header.message_id);
            (void)send_ack(header.message_id);
        }
        return SPW_OK;
    }

    const bool start = (header.flags & FlagFragmentStart) != 0u;
    const bool end = (header.flags & FlagFragmentEnd) != 0u;
    if (start) {
        clear_reassembly();
        reassembly_active_ = true;
        reassembly_message_id_ = header.message_id;
        reassembly_total_size_ = header.total_size;
        reassembly_terminator_ = terminator;
    }

    if (!reassembly_active_ || header.message_id != reassembly_message_id_ ||
        header.total_size != reassembly_total_size_ ||
        header.fragment_offset != reassembly_received_ ||
        terminator != reassembly_terminator_) {
        clear_reassembly();
        statistics_.dropped_packets++;
        return SPW_ERR_BACKEND;
    }

    if (header.payload_size != 0u) {
        std::memcpy(reassembly_.data() + header.fragment_offset,
                    payload,
                    header.payload_size);
    }
    reassembly_received_ += header.payload_size;

    if (end) {
        if (reassembly_received_ != reassembly_total_size_) {
            clear_reassembly();
            statistics_.dropped_packets++;
            return SPW_ERR_BACKEND;
        }
        if (pending_packet_valid_) {
            clear_reassembly();
            return SPW_ERR_RESOURCE_EXHAUSTED;
        }
        std::memcpy(pending_packet_.data(), reassembly_.data(), reassembly_total_size_);
        pending_packet_size_ = reassembly_total_size_;
        pending_packet_terminator_ = reassembly_terminator_;
        pending_packet_valid_ = true;
        clear_reassembly();
        if (ack_required) {
            remember_delivered(MessageType::Data, header.message_id);
            (void)send_ack(header.message_id);
        }
    }
    return SPW_OK;
}

spw_result_t UdpBackend::pump_one(spw_timeout_us_t timeout_us) noexcept {
    maybe_send_keepalive();

    const spw_timeout_us_t keepalive_slice =
        static_cast<spw_timeout_us_t>(config_.keepalive_interval_ms) * 1000u;
    spw_timeout_us_t service_slice = keepalive_slice;
    if (pending_tx_kind_ != PendingTxKind::None) {
        const spw_timeout_us_t ack_slice =
            static_cast<spw_timeout_us_t>(config_.ack_timeout_ms) * 1000u;
        service_slice = min_timeout(service_slice, ack_slice);
    }
    const spw_timeout_us_t wait_timeout = min_timeout(timeout_us, service_slice);
    const spw_result_t wait_result = wait_readable(wait_timeout);
    if (wait_result == SPW_ERR_TIMEOUT) {
        maybe_send_keepalive();
        refresh_peer_state();
        if (timeout_us == SPW_TIMEOUT_INFINITE || wait_timeout < timeout_us) {
            return SPW_OK;
        }
        return SPW_ERR_TIMEOUT;
    }
    if (wait_result != SPW_OK) {
        return wait_result;
    }

    sockaddr_in source{};
    socklen_t source_size = sizeof(source);
    const ssize_t received = ::recvfrom(socket_fd_, rx_datagram_.data(), rx_datagram_.size(), 0,
                                        reinterpret_cast<sockaddr*>(&source), &source_size);
    if (received < 0) {
        return errno == EINTR ? SPW_ERR_TIMEOUT : SPW_ERR_BACKEND;
    }
    if (!source_matches(config_, source)) {
        return SPW_OK;
    }

    Header header{};
    if (decode_header(rx_datagram_.data(), static_cast<std::size_t>(received), header) !=
        DecodeResult::Ok ||
        static_cast<std::size_t>(received) != kHeaderSize + header.payload_size) {
        statistics_.dropped_packets++;
        return SPW_OK;
    }
    if (header.link_id != config_.link_id) {
        return SPW_OK;
    }

    note_peer_activity();
    const auto* payload = rx_datagram_.data() + kHeaderSize;
    switch (header.type) {
        case MessageType::Data:
            if (header.total_size > max_packet_size) {
                statistics_.dropped_packets++;
                return SPW_OK;
            }
            return process_data(header, payload);
        case MessageType::TimeCode:
            return process_time_code(header, payload);
        case MessageType::Keepalive:
            return process_keepalive(header, payload);
        case MessageType::Ack:
            return process_ack(header);
        case MessageType::LinkControl:
            return SPW_OK;
    }
    return SPW_OK;
}

spw_result_t UdpBackend::ensure_peer(spw_timeout_us_t timeout_us) noexcept {
    refresh_peer_state();
    if (state_ == SPW_LINK_RUN && peer_is_current()) {
        return SPW_OK;
    }
    if (state_ != SPW_LINK_CONNECTING && state_ != SPW_LINK_ERROR_WAIT &&
        state_ != SPW_LINK_RUN) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }

    Deadline deadline(timeout_us);
    do {
        maybe_send_keepalive();
        const spw_result_t result = pump_one(deadline.remaining());
        refresh_peer_state();
        if (state_ == SPW_LINK_RUN && peer_is_current()) {
            return SPW_OK;
        }
        if (result != SPW_OK && result != SPW_ERR_TIMEOUT &&
            result != SPW_ERR_RESOURCE_EXHAUSTED) {
            return result;
        }
        if (deadline.expired() || timeout_us == SPW_TIMEOUT_IMMEDIATE) {
            return SPW_ERR_LINK_UNAVAILABLE;
        }
    } while (true);
}

spw_result_t UdpBackend::wait_for_tx_slot(spw_timeout_us_t timeout_us) noexcept {
    if (pending_tx_kind_ == PendingTxKind::None) {
        return SPW_OK;
    }

    Deadline deadline(timeout_us);
    do {
        const spw_result_t service_result = service_pending_tx();
        if (pending_tx_kind_ == PendingTxKind::None) {
            return SPW_OK;
        }
        if (service_result == SPW_ERR_LINK_UNAVAILABLE) {
            return service_result;
        }
        if (service_result != SPW_OK && service_result != SPW_ERR_TIMEOUT) {
            return service_result;
        }

        const spw_timeout_us_t retry_slice =
            static_cast<spw_timeout_us_t>(config_.ack_timeout_ms) * 1000u;
        const spw_result_t pump_result = pump_one(
            min_timeout(deadline.remaining(), retry_slice));
        if (pump_result != SPW_OK && pump_result != SPW_ERR_TIMEOUT &&
            pump_result != SPW_ERR_RESOURCE_EXHAUSTED) {
            return pump_result;
        }
        if (pending_tx_kind_ == PendingTxKind::None) {
            return SPW_OK;
        }
        if (deadline.expired() || timeout_us == SPW_TIMEOUT_IMMEDIATE) {
            return SPW_ERR_TIMEOUT;
        }
    } while (true);
}

spw_result_t UdpBackend::send(const spw_packet_t& packet,
                              spw_timeout_us_t timeout_us) noexcept {
    if ((packet.length != 0u && packet.data == nullptr) ||
        packet.length > max_packet_size || !valid_terminator(packet.terminator)) {
        return SPW_ERR_INVALID_PACKET;
    }

    Deadline deadline(timeout_us);
    const spw_result_t peer_result = ensure_peer(deadline.remaining());
    if (peer_result != SPW_OK) {
        return peer_result;
    }
    const spw_result_t slot_result = wait_for_tx_slot(deadline.remaining());
    if (slot_result != SPW_OK) {
        return slot_result;
    }

    if (packet.length != 0u) {
        std::memcpy(pending_tx_packet_.data(), packet.data, packet.length);
    }
    pending_tx_packet_size_ = packet.length;
    pending_tx_terminator_ = packet.terminator;
    pending_tx_kind_ = PendingTxKind::Data;
    pending_tx_message_id_ = take_nonzero(next_message_id_);
    pending_tx_retries_ = 0u;
    pending_tx_last_send_ = {};

    (void)send_keepalive(SPW_TIMEOUT_IMMEDIATE);
    const spw_result_t result = transmit_pending(deadline.remaining());
    if (result != SPW_OK) {
        clear_pending_tx();
        return result;
    }

    statistics_.tx_packets++;
    statistics_.tx_bytes += packet.length;
    if (packet.terminator == SPW_TERMINATOR_EEP) {
        statistics_.eep_packets++;
    }
    return SPW_OK;
}

spw_result_t UdpBackend::receive(spw_packet_t& packet,
                                 spw_timeout_us_t timeout_us) noexcept {
    Deadline deadline(timeout_us);
    const spw_result_t peer_result = ensure_peer(deadline.remaining());
    if (peer_result != SPW_OK && !pending_packet_valid_) {
        return peer_result;
    }

    while (!pending_packet_valid_) {
        const spw_result_t service_result = service_pending_tx();
        if (service_result == SPW_ERR_LINK_UNAVAILABLE) {
            return service_result;
        }
        const spw_result_t result = pump_one(deadline.remaining());
        if (result != SPW_OK && result != SPW_ERR_RESOURCE_EXHAUSTED &&
            result != SPW_ERR_TIMEOUT) {
            return result;
        }
        if (!pending_packet_valid_ &&
            (deadline.expired() || timeout_us == SPW_TIMEOUT_IMMEDIATE)) {
            refresh_peer_state();
            return state_ == SPW_LINK_ERROR_WAIT ? SPW_ERR_LINK_UNAVAILABLE
                                                  : SPW_ERR_TIMEOUT;
        }
    }

    packet.length = pending_packet_size_;
    packet.terminator = pending_packet_terminator_;
    if (packet.capacity < pending_packet_size_ ||
        (pending_packet_size_ != 0u && packet.data == nullptr)) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }
    if (pending_packet_size_ != 0u) {
        std::memcpy(packet.data, pending_packet_.data(), pending_packet_size_);
    }
    pending_packet_valid_ = false;
    statistics_.rx_packets++;
    statistics_.rx_bytes += packet.length;
    return SPW_OK;
}

spw_result_t UdpBackend::send_time_code(const spw_time_code_t& time_code,
                                        spw_timeout_us_t timeout_us) noexcept {
    if (!valid_time_code(time_code)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    Deadline deadline(timeout_us);
    const spw_result_t peer_result = ensure_peer(deadline.remaining());
    if (peer_result != SPW_OK) {
        return peer_result;
    }
    const spw_result_t slot_result = wait_for_tx_slot(deadline.remaining());
    if (slot_result != SPW_OK) {
        return slot_result;
    }

    pending_tx_time_code_ = time_code;
    pending_tx_kind_ = PendingTxKind::TimeCode;
    pending_tx_message_id_ = take_nonzero(next_message_id_);
    pending_tx_retries_ = 0u;
    pending_tx_last_send_ = {};

    (void)send_keepalive(SPW_TIMEOUT_IMMEDIATE);
    const spw_result_t result = transmit_pending(deadline.remaining());
    if (result != SPW_OK) {
        clear_pending_tx();
        return result;
    }
    statistics_.tx_time_codes++;
    return SPW_OK;
}

spw_result_t UdpBackend::receive_time_code(spw_time_code_t& time_code,
                                           spw_timeout_us_t timeout_us) noexcept {
    Deadline deadline(timeout_us);
    const spw_result_t peer_result = ensure_peer(deadline.remaining());
    if (peer_result != SPW_OK && time_code_count_ == 0u) {
        return peer_result;
    }

    while (time_code_count_ == 0u) {
        const spw_result_t service_result = service_pending_tx();
        if (service_result == SPW_ERR_LINK_UNAVAILABLE) {
            return service_result;
        }
        const spw_result_t result = pump_one(deadline.remaining());
        if (result != SPW_OK && result != SPW_ERR_RESOURCE_EXHAUSTED &&
            result != SPW_ERR_TIMEOUT) {
            return result;
        }
        if (time_code_count_ == 0u &&
            (deadline.expired() || timeout_us == SPW_TIMEOUT_IMMEDIATE)) {
            refresh_peer_state();
            return state_ == SPW_LINK_ERROR_WAIT ? SPW_ERR_LINK_UNAVAILABLE
                                                  : SPW_ERR_TIMEOUT;
        }
    }

    time_code = time_codes_[time_code_head_];
    time_code_head_ = (time_code_head_ + 1u) % time_code_queue_depth;
    time_code_count_--;
    statistics_.rx_time_codes++;
    return SPW_OK;
}

spw_result_t UdpBackend::get_statistics(spw_statistics_t& statistics) const noexcept {
    statistics = statistics_;
    return SPW_OK;
}

spw_result_t UdpBackend::clear_statistics() noexcept {
    statistics_ = {};
    return SPW_OK;
}

} // namespace spwkit::detail
