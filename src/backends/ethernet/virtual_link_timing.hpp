// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

namespace spwkit::ethernet {

enum class VirtualLinkEvent : std::uint8_t {
    Data = 0u,
    TimeCode,
};

/*
 * Deterministic SpaceWire-side timing model used by distributed simulation.
 *
 * This is deliberately an effective logical timing model rather than a
 * character/signal-accurate PHY model. DATA charges payload bytes plus one
 * logical terminator octet; TIME_CODE charges its two-byte logical event.
 * Fixed latency is added once per logical event. Transport ACK/KEEPALIVE and
 * reliability retransmissions are intentionally outside this model.
 */
class VirtualLinkTiming {
public:
    constexpr VirtualLinkTiming(std::uint64_t link_bps = 0u,
                                std::uint32_t latency_us = 0u) noexcept
        : link_bps_(link_bps), latency_us_(latency_us) {}

    constexpr bool enabled() const noexcept {
        return link_bps_ != 0u || latency_us_ != 0u;
    }

    constexpr std::uint64_t serialization_us(VirtualLinkEvent event,
                                             std::size_t payload_size) const noexcept {
        if (link_bps_ == 0u) {
            return 0u;
        }

        const std::uint64_t effective_octets =
            event == VirtualLinkEvent::Data
                ? static_cast<std::uint64_t>(payload_size) + 1u
                : 2u;
        const std::uint64_t bit_microseconds = effective_octets * 8u * 1000000u;
        const std::uint64_t whole = bit_microseconds / link_bps_;
        return whole + (bit_microseconds % link_bps_ != 0u ? 1u : 0u);
    }

    constexpr std::uint64_t delay_us(VirtualLinkEvent event,
                                     std::size_t payload_size) const noexcept {
        return serialization_us(event, payload_size) + latency_us_;
    }

    constexpr std::uint64_t link_bps() const noexcept {
        return link_bps_;
    }

    constexpr std::uint32_t latency_us() const noexcept {
        return latency_us_;
    }

private:
    std::uint64_t link_bps_{0u};
    std::uint32_t latency_us_{0u};
};

} // namespace spwkit::ethernet
