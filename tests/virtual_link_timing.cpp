// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/virtual_link_timing.hpp"

#include <cassert>

using spwkit::ethernet::VirtualLinkEvent;
using spwkit::ethernet::VirtualLinkTiming;

int main() {
    const VirtualLinkTiming disabled{};
    assert(!disabled.enabled());
    assert(disabled.delay_us(VirtualLinkEvent::Data, 4096u) == 0u);

    const VirtualLinkTiming latency_only{0u, 250u};
    assert(latency_only.enabled());
    assert(latency_only.serialization_us(VirtualLinkEvent::Data, 10u) == 0u);
    assert(latency_only.delay_us(VirtualLinkEvent::Data, 10u) == 250u);

    /* 124 payload octets + one terminator = 1000 effective bits at 1 Mbit/s. */
    const VirtualLinkTiming one_mbps{1000000u, 0u};
    assert(one_mbps.serialization_us(VirtualLinkEvent::Data, 124u) == 1000u);
    assert(one_mbps.serialization_us(VirtualLinkEvent::Data, 0u) == 8u);
    assert(one_mbps.serialization_us(VirtualLinkEvent::TimeCode, 2u) == 16u);

    const VirtualLinkTiming rounded{3000000u, 7u};
    assert(rounded.serialization_us(VirtualLinkEvent::Data, 1u) == 6u);
    assert(rounded.delay_us(VirtualLinkEvent::Data, 1u) == 13u);
    assert(rounded.link_bps() == 3000000u);
    assert(rounded.latency_us() == 7u);
    return 0;
}
