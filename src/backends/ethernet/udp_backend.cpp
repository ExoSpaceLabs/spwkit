// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/udp_backend.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <ctime>

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

UdpBackend::UdpBackend(const spw_udp_config_t& config) noexcept
    : config_(config),
      virtual_timing_(config.virtual_link_bps, config.virtual_latency_us),
      fault_injector_(config) {}

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
    for (std::size_t i = 0u; i < SPW_UDP_FAULT_RULE_COUNT; ++i) {
        if (!FaultInjector::valid_rule(config_.fault_rules[i])) {
            return SPW_ERR_INVALID_ARGUMENT;
        }
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
    reassembly_.reset();
    reassembly_last_fragment_ = {};
}

void UdpBackend::expire_reassembly() noexcept {
    if (!reassembly_.active() || reassembly_last_fragment_ == TimePoint{}) {
        return;
    }
    const auto timeout = std::chrono::milliseconds(config_.peer_timeout_ms);
    if (Clock::now() - reassembly_last_fragment_ >= timeout) {
        clear_reassembly();
    }
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

void UdpBackend::clear_retired_sessions() noexcept {
    retired_session_head_ = 0u;
    retired_session_count_ = 0u;
}

void UdpBackend::clear_reordered_datagram() noexcept {
    reordered_datagram_size_ = 0u;
    reordered_datagram_valid_ = false;
}

spw_result_t UdpBackend::start() noexcept {
    if (socket_fd_ < 0) {
        return SPW_ERR_INVALID_STATE;
    }

    clear_reassembly();
    clear_pending_tx();
    clear_recent_messages();
    clear_retired_sessions();
    clear_reordered_datagram();
    fault_injector_.reset();
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
    clear_retired_sessions();
    clear_reordered_datagram();
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
    fault_injector_.reset();
    state_ = SPW_LINK_ERROR_RESET;
    clear_reassembly();
    clear_pending_tx();
    clear_recent_messages();
    clear_retired_sessions();
    clear_reordered_datagram();
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
    clear_reassembly();
    state_ = SPW_LINK_ERROR_WAIT;
}

void UdpBackend::refresh_peer_state() noexcept {
    expire_reassembly();
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
    capabilities.bits = SPW_CAP_EEP | SPW_CAP_TIME_CODE | SPW_CAP_STATISTICS |
                        SPW_CAP_RATE_CONTROL | SPW_CAP_FAULT_INJECTION;
    capabilities.max_packet_size = max_packet_size;
    capabilities.tx_queue_depth = 1u;
    capabilities.rx_queue_depth = 1u;
    capabilities.buffer_alignment = alignof(std::max_align_t);
    return SPW_OK;
}

spw_result_t UdpBackend::send_datagram_raw(const std::uint8_t* bytes,
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

spw_result_t UdpBackend::wait_transport_fault_delay(
    std::uint32_t delay_us, spw_timeout_us_t timeout_us) noexcept {
    if (delay_us == 0u) {
        return SPW_OK;
    }
    if (timeout_us != SPW_TIMEOUT_INFINITE && timeout_us < delay_us) {
        return SPW_ERR_TIMEOUT;
    }

    timespec request{};
    request.tv_sec = static_cast<time_t>(delay_us / 1000000u);
    request.tv_nsec = static_cast<long>((delay_us % 1000000u) * 1000u);
    timespec remaining{};
    while (::nanosleep(&request, &remaining) != 0) {
        if (errno != EINTR) {
            return SPW_ERR_BACKEND;
        }
        request = remaining;
    }
    return SPW_OK;
}

spw_result_t UdpBackend::send_datagram(const std::uint8_t* bytes,
                                       std::size_t size,
                                       spw_timeout_us_t timeout_us) noexcept {
    if (reordered_datagram_valid_) {
        const spw_result_t current = send_datagram_raw(bytes, size, timeout_us);
        if (current != SPW_OK) {
            return current;
        }
        const spw_result_t held = send_datagram_raw(
            reordered_datagram_.data(), reordered_datagram_size_, timeout_us);
        clear_reordered_datagram();
        return held;
    }

    Header header{};
    if (decode_header(bytes, size, header) != DecodeResult::Ok) {
        return send_datagram_raw(bytes, size, timeout_us);
    }

    const FaultInjector::Decision decision = fault_injector_.transport(header.type);
    switch (decision.action) {
    case SPW_UDP_FAULT_ACTION_TRANSPORT_DROP:
        ++fault_statistics_.transport_drops;
        ++statistics_.dropped_packets;
        return SPW_OK;

    case SPW_UDP_FAULT_ACTION_TRANSPORT_DUPLICATE: {
        ++fault_statistics_.transport_duplicates;
        const spw_result_t first = send_datagram_raw(bytes, size, timeout_us);
        return first == SPW_OK ? send_datagram_raw(bytes, size, timeout_us) : first;
    }

    case SPW_UDP_FAULT_ACTION_TRANSPORT_REORDER:
        ++fault_statistics_.transport_reorders;
        if (size > reordered_datagram_.size()) {
            return SPW_ERR_BACKEND;
        }
        std::memcpy(reordered_datagram_.data(), bytes, size);
        reordered_datagram_size_ = size;
        reordered_datagram_valid_ = true;
        return SPW_OK;

    case SPW_UDP_FAULT_ACTION_TRANSPORT_DELAY: {
        ++fault_statistics_.transport_delays;
        const spw_result_t delay = wait_transport_fault_delay(
            decision.delay_us, timeout_us);
        return delay == SPW_OK ? send_datagram_raw(bytes, size, timeout_us) : delay;
    }

    case SPW_UDP_FAULT_ACTION_NONE:
    case SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP:
        return send_datagram_raw(bytes, size, timeout_us);
    }
    return send_datagram_raw(bytes, size, timeout_us);
}

spw_result_t UdpBackend::wait_virtual_link_delay(
    std::uint64_t delay_us, spw_timeout_us_t timeout_us) noexcept {
    if (delay_us == 0u) {
        return SPW_OK;
    }
    if (timeout_us != SPW_TIMEOUT_INFINITE &&
        static_cast<std::uint64_t>(timeout_us) < delay_us) {
        return SPW_ERR_TIMEOUT;
    }

    Deadline deadline(timeout_us);
    const TimePoint target = Clock::now() + std::chrono::microseconds(delay_us);
    while (Clock::now() < target) {
        const auto now = Clock::now();
        const auto remaining_count =
            std::chrono::duration_cast<std::chrono::microseconds>(target - now).count();
        const spw_timeout_us_t timing_slice = remaining_count <= 0
            ? SPW_TIMEOUT_IMMEDIATE
            : static_cast<spw_timeout_us_t>(remaining_count);
        const spw_result_t result = pump_one(
            min_timeout(deadline.remaining(), timing_slice));
        if (result != SPW_OK && result != SPW_ERR_TIMEOUT &&
            result != SPW_ERR_RESOURCE_EXHAUSTED) {
            return result;
        }
        if (Clock::now() >= target) {
            return SPW_OK;
        }
        if (deadline.expired()) {
            return SPW_ERR_TIMEOUT;
        }
    }
    return SPW_OK;
}

spw_result_t UdpBackend::send_ack(std::uint32_t message_id) noexcept {
    if (message_id == 0u) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (remote_session_id_ == 0u) {
        return SPW_ERR_INVALID_STATE;
    }

    Header header{};
    header.type = MessageType::Ack;
    header.payload_size = static_cast<std::uint16_t>(kAckPayloadSize);
    header.link_id = config_.link_id;
    header.session_id = local_session_id_;
    header.sequence = take_nonzero(next_sequence_);
    header.message_id = message_id;
    header.total_size = static_cast<std::uint32_t>(kAckPayloadSize);
    if (!encode_header(header, control_datagram_.data(), control_datagram_.size()) ||
        !encode_ack_payload(remote_session_id_,
                            control_datagram_.data() + kHeaderSize,
                            kAckPayloadSize)) {
        return SPW_ERR_BACKEND;
    }
    return send_datagram(control_datagram_.data(),
                         kHeaderSize + kAckPayloadSize,
                         SPW_TIMEOUT_IMMEDIATE);
}

spw_result_t UdpBackend::send_keepalive(spw_timeout_us_t timeout_us) noexcept {
    Header header{};
    header.type = MessageType::Keepalive;
    header.link_id = config_.link_id;
    header.session_id = local_session_id_;
    header.sequence = take_nonzero(next_sequence_);
    if (!encode_header(header, control_datagram_.data(), control_datagram_.size())) {
        return SPW_ERR_BACKEND;
    }

    const spw_result_t result = send_datagram(control_datagram_.data(),
                                               kHeaderSize,
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

bool UdpBackend::is_retired_session(std::uint64_t session_id) const noexcept {
    if (session_id == 0u) {
        return false;
    }
    for (std::size_t i = 0u; i < retired_session_count_; ++i) {
        const std::size_t index = (retired_session_head_ + i) % retired_session_depth;
        if (retired_sessions_[index] == session_id) {
            return true;
        }
    }
    return false;
}

void UdpBackend::remember_retired_session(std::uint64_t session_id) noexcept {
    if (session_id == 0u || is_retired_session(session_id)) {
        return;
    }
    if (retired_session_count_ < retired_session_depth) {
        const std::size_t index =
            (retired_session_head_ + retired_session_count_) % retired_session_depth;
        retired_sessions_[index] = session_id;
        retired_session_count_++;
        return;
    }
    retired_sessions_[retired_session_head_] = session_id;
    retired_session_head_ = (retired_session_head_ + 1u) % retired_session_depth;
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

bool UdpBackend::reset_remote_session(std::uint64_t session_id) noexcept {
    if (session_id == 0u || is_retired_session(session_id)) {
        return false;
    }
    if (remote_session_id_ == session_id) {
        note_peer_activity();
        return true;
    }
    if (remote_session_id_ != 0u) {
        remember_retired_session(remote_session_id_);
    }
    remote_session_id_ = session_id;
    clear_reassembly();
    clear_recent_messages();
    if (pending_tx_kind_ != PendingTxKind::None) {
        pending_tx_retries_ = 0u;
        pending_tx_failed_ = false;
        pending_tx_last_send_ = {};
    }
    note_peer_activity();
    return true;
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
        header.session_id = local_session_id_;
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
        header.session_id = local_session_id_;
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

spw_result_t UdpBackend::process_ack(const Header& header,
                                     const std::uint8_t* payload) noexcept {
    std::uint64_t acknowledged_session_id = 0u;
    if (!decode_ack_payload(payload, header.payload_size, acknowledged_session_id)) {
        statistics_.dropped_packets++;
        return SPW_OK;
    }
    if (acknowledged_session_id != local_session_id_) {
        return SPW_OK;
    }
    if (pending_tx_kind_ != PendingTxKind::None &&
        header.message_id == pending_tx_message_id_) {
        clear_pending_tx();
    }
    return SPW_OK;
}

spw_result_t UdpBackend::process_keepalive(const Header& header) noexcept {
    (void)reset_remote_session(header.session_id);
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

    expire_reassembly();
    const FragmentReassembler::Result result = reassembly_.push(header, payload);
    if (result == FragmentReassembler::Result::Invalid ||
        result == FragmentReassembler::Result::Conflict) {
        statistics_.dropped_packets++;
        return SPW_OK;
    }

    reassembly_last_fragment_ = Clock::now();
    if (result != FragmentReassembler::Result::Complete) {
        return SPW_OK;
    }

    if (pending_packet_valid_) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    const std::size_t completed_size = reassembly_.size();
    const std::uint32_t completed_message_id = reassembly_.message_id();
    const bool completed_ack_required = reassembly_.ack_required();
    if (completed_size != 0u) {
        std::memcpy(pending_packet_.data(), reassembly_.data(), completed_size);
    }
    pending_packet_size_ = completed_size;
    pending_packet_terminator_ =
        reassembly_.eep() ? SPW_TERMINATOR_EEP : SPW_TERMINATOR_EOP;
    pending_packet_valid_ = true;
    clear_reassembly();

    if (completed_ack_required) {
        remember_delivered(MessageType::Data, completed_message_id);
        (void)send_ack(completed_message_id);
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

    const auto* payload = rx_datagram_.data() + kHeaderSize;
    if (header.type == MessageType::Keepalive) {
        return process_keepalive(header);
    }
    if (remote_session_id_ == 0u || header.session_id != remote_session_id_) {
        return SPW_OK;
    }
    note_peer_activity();
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
            return SPW_OK;
        case MessageType::Ack:
            return process_ack(header, payload);
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

    const spw_result_t timing_result = wait_virtual_link_delay(
        virtual_timing_.delay_us(VirtualLinkEvent::Data, packet.length),
        deadline.remaining());
    if (timing_result != SPW_OK) {
        return timing_result;
    }

    spw_terminator_t effective_terminator = packet.terminator;
    if (packet.terminator == SPW_TERMINATOR_EOP && fault_injector_.spacewire_eep()) {
        effective_terminator = SPW_TERMINATOR_EEP;
        ++fault_statistics_.spacewire_eep_injections;
    }

    if (packet.length != 0u) {
        std::memcpy(pending_tx_packet_.data(), packet.data, packet.length);
    }
    pending_tx_packet_size_ = packet.length;
    pending_tx_terminator_ = effective_terminator;
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
    if (pending_tx_terminator_ == SPW_TERMINATOR_EEP) {
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

    const spw_result_t timing_result = wait_virtual_link_delay(
        virtual_timing_.delay_us(VirtualLinkEvent::TimeCode, kTimeCodePayloadSize),
        deadline.remaining());
    if (timing_result != SPW_OK) {
        return timing_result;
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

spw_result_t UdpBackend::get_fault_statistics(
    spw_fault_statistics_t& statistics) const noexcept {
    statistics = fault_statistics_;
    return SPW_OK;
}

spw_result_t UdpBackend::clear_fault_statistics() noexcept {
    fault_statistics_ = {};
    return SPW_OK;
}

} // namespace spwkit::detail
