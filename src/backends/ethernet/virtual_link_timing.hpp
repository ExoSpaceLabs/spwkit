// SPDX-License-Identifier: Apache-2.0
#pragma once

/* C++ test compatibility over the C11 virtual-link timing implementation. */
#include "backends/ethernet/virtual_link_timing.h"

#include <cstddef>
#include <cstdint>

namespace spwkit::ethernet {

enum class VirtualLinkEvent : std::uint8_t {
    Data = SPW_VIRTUAL_LINK_EVENT_DATA,
    TimeCode = SPW_VIRTUAL_LINK_EVENT_TIME_CODE,
};

class VirtualLinkTiming {
public:
    constexpr VirtualLinkTiming(std::uint64_t link_bps = 0u,
                                std::uint32_t latency_us = 0u) noexcept
        : value_{link_bps, latency_us} {}

    constexpr bool enabled() const noexcept {
        return value_.link_bps != 0u || value_.latency_us != 0u;
    }

    std::uint64_t serialization_us(VirtualLinkEvent event,
                                   std::size_t payload_size) const noexcept {
        return spw_virtual_link_serialization_us(
            &value_, static_cast<std::uint8_t>(event), payload_size);
    }

    std::uint64_t delay_us(VirtualLinkEvent event,
                           std::size_t payload_size) const noexcept {
        return spw_virtual_link_delay_us(
            &value_, static_cast<std::uint8_t>(event), payload_size);
    }

    constexpr std::uint64_t link_bps() const noexcept { return value_.link_bps; }
    constexpr std::uint32_t latency_us() const noexcept { return value_.latency_us; }

private:
    spw_virtual_link_timing_t value_;
};

} // namespace spwkit::ethernet
