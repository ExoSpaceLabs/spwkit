// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/udp_backend.hpp"

#include "backends/ethernet/vspw_tp.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace spwkit::detail {
namespace {

using namespace spwkit::ethernet::vspw_tp;

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

std::uint8_t terminator_flag(spw_terminator_t terminator) noexcept {
    return terminator == SPW_TERMINATOR_EEP ? FlagEep : FlagEop;
}

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
        config_.reserved != 0u) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(config_.local_port);
    if (::inet_pton(AF_INET, config_.local_address, &local.sin_addr) != 1) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(config_.remote_port);
    if (::inet_pton(AF_INET, config_.remote_address, &remote.sin_addr) != 1) {
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

spw_result_t UdpBackend::start() noexcept {
    if (socket_fd_ < 0) {
        return SPW_ERR_INVALID_STATE;
    }
    state_ = SPW_LINK_RUN;
    return SPW_OK;
}

spw_result_t UdpBackend::stop() noexcept {
    if (socket_fd_ < 0) {
        return SPW_ERR_INVALID_STATE;
    }
    state_ = SPW_LINK_READY;
    clear_reassembly();
    pending_packet_valid_ = false;
    time_code_head_ = 0u;
    time_code_count_ = 0u;
    return SPW_OK;
}

spw_result_t UdpBackend::reset() noexcept {
    if (socket_fd_ < 0) {
        return SPW_ERR_INVALID_STATE;
    }
    state_ = SPW_LINK_ERROR_RESET;
    clear_reassembly();
    pending_packet_valid_ = false;
    time_code_head_ = 0u;
    time_code_count_ = 0u;
    return SPW_OK;
}

spw_result_t UdpBackend::get_link_state(spw_link_state_t& state) const noexcept {
    state = state_;
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
    if (ready < 0) {
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

spw_result_t UdpBackend::send(const spw_packet_t& packet,
                              spw_timeout_us_t timeout_us) noexcept {
    if (state_ != SPW_LINK_RUN) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    if ((packet.length != 0u && packet.data == nullptr) ||
        packet.length > max_packet_size || !valid_terminator(packet.terminator)) {
        return SPW_ERR_INVALID_PACKET;
    }

    const std::uint32_t message_id = next_message_id_++;
    const std::size_t fragment_size = config_.fragment_payload_size;
    const bool fragmented = packet.length > fragment_size;
    std::size_t offset = 0u;

    do {
        const std::size_t remaining = packet.length - offset;
        const std::size_t payload_size = fragmented
            ? std::min(fragment_size, remaining)
            : remaining;

        Header header{};
        header.type = MessageType::Data;
        header.flags = terminator_flag(packet.terminator);
        if (fragmented && offset == 0u) {
            header.flags |= FlagFragmentStart;
        }
        if (fragmented && offset + payload_size == packet.length) {
            header.flags |= FlagFragmentEnd;
        }
        header.payload_size = static_cast<std::uint16_t>(payload_size);
        header.link_id = config_.link_id;
        header.sequence = next_sequence_++;
        header.message_id = message_id;
        header.fragment_offset = static_cast<std::uint32_t>(offset);
        header.total_size = static_cast<std::uint32_t>(packet.length);

        if (!encode_header(header, datagram_.data(), datagram_.size())) {
            return SPW_ERR_INVALID_PACKET;
        }
        if (payload_size != 0u) {
            std::memcpy(datagram_.data() + kHeaderSize,
                        packet.data + offset,
                        payload_size);
        }
        const spw_result_t result =
            send_datagram(datagram_.data(), kHeaderSize + payload_size, timeout_us);
        if (result != SPW_OK) {
            statistics_.dropped_packets++;
            return result;
        }
        offset += payload_size;
    } while (offset < packet.length);

    statistics_.tx_packets++;
    statistics_.tx_bytes += packet.length;
    if (packet.terminator == SPW_TERMINATOR_EEP) {
        statistics_.eep_packets++;
    }
    return SPW_OK;
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
    if (ready < 0) {
        return SPW_ERR_BACKEND;
    }
    return SPW_OK;
}

void UdpBackend::clear_reassembly() noexcept {
    reassembly_message_id_ = 0u;
    reassembly_total_size_ = 0u;
    reassembly_received_ = 0u;
    reassembly_terminator_ = SPW_TERMINATOR_EOP;
    reassembly_active_ = false;
}

spw_result_t UdpBackend::process_data(const std::uint8_t* datagram,
                                      std::size_t datagram_size) noexcept {
    Header header{};
    if (decode_header(datagram, datagram_size, header) != DecodeResult::Ok) {
        statistics_.dropped_packets++;
        return SPW_ERR_BACKEND;
    }
    if (header.link_id != config_.link_id || header.type != MessageType::Data ||
        header.total_size > max_packet_size) {
        statistics_.dropped_packets++;
        return SPW_OK;
    }

    const auto* payload = datagram + kHeaderSize;
    const bool fragmented = header.total_size != header.payload_size;
    const spw_terminator_t terminator =
        (header.flags & FlagEep) != 0u ? SPW_TERMINATOR_EEP : SPW_TERMINATOR_EOP;

    if (!fragmented) {
        if (pending_packet_valid_) {
            statistics_.dropped_packets++;
            return SPW_ERR_RESOURCE_EXHAUSTED;
        }
        if (header.payload_size != 0u) {
            std::memcpy(pending_packet_.data(), payload, header.payload_size);
        }
        pending_packet_size_ = header.payload_size;
        pending_packet_terminator_ = terminator;
        pending_packet_valid_ = true;
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
        if (reassembly_received_ != reassembly_total_size_ || pending_packet_valid_) {
            clear_reassembly();
            statistics_.dropped_packets++;
            return SPW_ERR_RESOURCE_EXHAUSTED;
        }
        std::memcpy(pending_packet_.data(), reassembly_.data(), reassembly_total_size_);
        pending_packet_size_ = reassembly_total_size_;
        pending_packet_terminator_ = reassembly_terminator_;
        pending_packet_valid_ = true;
        clear_reassembly();
    }
    return SPW_OK;
}

spw_result_t UdpBackend::pump_one(spw_timeout_us_t timeout_us) noexcept {
    const spw_result_t wait_result = wait_readable(timeout_us);
    if (wait_result != SPW_OK) {
        return wait_result;
    }

    sockaddr_in source{};
    socklen_t source_size = sizeof(source);
    const ssize_t received = ::recvfrom(socket_fd_, datagram_.data(), datagram_.size(), 0,
                                        reinterpret_cast<sockaddr*>(&source), &source_size);
    if (received < 0) {
        return errno == EINTR ? SPW_ERR_TIMEOUT : SPW_ERR_BACKEND;
    }

    Header header{};
    if (decode_header(datagram_.data(), static_cast<std::size_t>(received), header) !=
        DecodeResult::Ok) {
        statistics_.dropped_packets++;
        return SPW_OK;
    }
    if (header.link_id != config_.link_id) {
        return SPW_OK;
    }

    if (header.type == MessageType::Data) {
        return process_data(datagram_.data(), static_cast<std::size_t>(received));
    }
    if (header.type == MessageType::TimeCode) {
        if (header.payload_size != 2u || time_code_count_ == time_code_queue_depth) {
            statistics_.dropped_packets++;
            return SPW_OK;
        }
        const std::size_t index = (time_code_head_ + time_code_count_) % time_code_queue_depth;
        time_codes_[index] = {datagram_[kHeaderSize], datagram_[kHeaderSize + 1u]};
        if (!valid_time_code(time_codes_[index])) {
            statistics_.dropped_packets++;
            return SPW_OK;
        }
        time_code_count_++;
        return SPW_OK;
    }

    return SPW_OK;
}

spw_result_t UdpBackend::receive(spw_packet_t& packet,
                                 spw_timeout_us_t timeout_us) noexcept {
    if (state_ != SPW_LINK_RUN) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    while (!pending_packet_valid_) {
        const spw_result_t result = pump_one(timeout_us);
        if (result != SPW_OK && result != SPW_ERR_RESOURCE_EXHAUSTED) {
            return result;
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
    if (state_ != SPW_LINK_RUN) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    if (!valid_time_code(time_code)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    Header header{};
    header.type = MessageType::TimeCode;
    header.payload_size = 2u;
    header.link_id = config_.link_id;
    header.sequence = next_sequence_++;
    header.message_id = next_message_id_++;
    header.fragment_offset = 0u;
    header.total_size = 2u;
    if (!encode_header(header, datagram_.data(), datagram_.size())) {
        return SPW_ERR_BACKEND;
    }
    datagram_[kHeaderSize] = time_code.time_count;
    datagram_[kHeaderSize + 1u] = time_code.control_flags;
    const spw_result_t result = send_datagram(datagram_.data(), kHeaderSize + 2u, timeout_us);
    if (result == SPW_OK) {
        statistics_.tx_time_codes++;
    }
    return result;
}

spw_result_t UdpBackend::receive_time_code(spw_time_code_t& time_code,
                                           spw_timeout_us_t timeout_us) noexcept {
    if (state_ != SPW_LINK_RUN) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    while (time_code_count_ == 0u) {
        const spw_result_t result = pump_one(timeout_us);
        if (result != SPW_OK && result != SPW_ERR_RESOURCE_EXHAUSTED) {
            return result;
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
