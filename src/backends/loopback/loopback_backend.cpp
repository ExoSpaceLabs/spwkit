// SPDX-License-Identifier: Apache-2.0

#include "backends/loopback/loopback_backend.hpp"

#include <algorithm>
#include <cstring>

namespace spwkit::detail {

bool LoopbackBackend::valid_terminator(spw_terminator_t terminator) noexcept {
    return terminator == SPW_TERMINATOR_EOP || terminator == SPW_TERMINATOR_EEP;
}

bool LoopbackBackend::valid_time_code(const spw_time_code_t& time_code) noexcept {
    return time_code.time_count <= 63u && time_code.control_flags == 0u;
}

void LoopbackBackend::clear_queues() noexcept {
    packets_ = PacketQueue{};
    time_codes_ = TimeCodeQueue{};
}

spw_result_t LoopbackBackend::start() noexcept {
    state_ = SPW_LINK_RUN;
    return SPW_OK;
}

spw_result_t LoopbackBackend::stop() noexcept {
    state_ = SPW_LINK_READY;
    return SPW_OK;
}

spw_result_t LoopbackBackend::reset() noexcept {
    clear_queues();
    state_ = SPW_LINK_ERROR_RESET;
    return SPW_OK;
}

spw_result_t LoopbackBackend::get_link_state(spw_link_state_t& state) const noexcept {
    state = state_;
    return SPW_OK;
}

spw_result_t LoopbackBackend::get_capabilities(spw_capabilities_t& capabilities) const noexcept {
    capabilities.bits = SPW_CAP_EEP |
                        SPW_CAP_TIME_CODE |
                        SPW_CAP_LINK_CONTROL |
                        SPW_CAP_STATISTICS;
    capabilities.max_packet_size = max_packet_size;
    capabilities.tx_queue_depth = packet_queue_depth;
    capabilities.rx_queue_depth = packet_queue_depth;
    capabilities.buffer_alignment = alignof(std::max_align_t);
    return SPW_OK;
}

spw_result_t LoopbackBackend::send(const spw_packet_t& packet,
                                   spw_timeout_us_t /*timeout_us*/) noexcept {
    if (state_ != SPW_LINK_RUN) {
        return SPW_ERR_INVALID_STATE;
    }
    if (!valid_terminator(packet.terminator)) {
        return SPW_ERR_INVALID_PACKET;
    }
    if (packet.length > max_packet_size) {
        return SPW_ERR_INVALID_PACKET;
    }
    if (packet.length > 0u && packet.data == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (packet.capacity != 0u && packet.capacity < packet.length) {
        return SPW_ERR_INVALID_PACKET;
    }
    if (packets_.count == packet_queue_depth) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    PacketSlot& slot = packets_.slots[packets_.tail];
    if (packet.length > 0u) {
        std::memcpy(slot.data.data(), packet.data, packet.length);
    }
    slot.length = packet.length;
    slot.terminator = packet.terminator;

    packets_.tail = (packets_.tail + 1u) % packet_queue_depth;
    ++packets_.count;

    ++statistics_.tx_packets;
    statistics_.tx_bytes += packet.length;
    if (packet.terminator == SPW_TERMINATOR_EEP) {
        ++statistics_.eep_packets;
    }
    return SPW_OK;
}

spw_result_t LoopbackBackend::receive(spw_packet_t& packet,
                                      spw_timeout_us_t /*timeout_us*/) noexcept {
    if (state_ != SPW_LINK_RUN) {
        return SPW_ERR_INVALID_STATE;
    }
    if (packets_.count == 0u) {
        return SPW_ERR_TIMEOUT;
    }

    const PacketSlot& slot = packets_.slots[packets_.head];
    packet.length = slot.length;
    packet.terminator = slot.terminator;

    if (slot.length > packet.capacity) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }
    if (slot.length > 0u && packet.data == nullptr) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    if (slot.length > 0u) {
        std::memcpy(packet.data, slot.data.data(), slot.length);
    }

    packets_.head = (packets_.head + 1u) % packet_queue_depth;
    --packets_.count;

    ++statistics_.rx_packets;
    statistics_.rx_bytes += slot.length;
    return SPW_OK;
}

spw_result_t LoopbackBackend::send_time_code(const spw_time_code_t& time_code,
                                             spw_timeout_us_t /*timeout_us*/) noexcept {
    if (state_ != SPW_LINK_RUN) {
        return SPW_ERR_INVALID_STATE;
    }
    if (!valid_time_code(time_code)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (time_codes_.count == time_code_queue_depth) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    time_codes_.slots[time_codes_.tail] = time_code;
    time_codes_.tail = (time_codes_.tail + 1u) % time_code_queue_depth;
    ++time_codes_.count;
    ++statistics_.tx_time_codes;
    return SPW_OK;
}

spw_result_t LoopbackBackend::receive_time_code(spw_time_code_t& time_code,
                                                spw_timeout_us_t /*timeout_us*/) noexcept {
    if (state_ != SPW_LINK_RUN) {
        return SPW_ERR_INVALID_STATE;
    }
    if (time_codes_.count == 0u) {
        return SPW_ERR_TIMEOUT;
    }

    time_code = time_codes_.slots[time_codes_.head];
    time_codes_.head = (time_codes_.head + 1u) % time_code_queue_depth;
    --time_codes_.count;
    ++statistics_.rx_time_codes;
    return SPW_OK;
}

spw_result_t LoopbackBackend::get_statistics(spw_statistics_t& statistics) const noexcept {
    statistics = statistics_;
    return SPW_OK;
}

spw_result_t LoopbackBackend::clear_statistics() noexcept {
    statistics_ = spw_statistics_t{};
    return SPW_OK;
}

} // namespace spwkit::detail
