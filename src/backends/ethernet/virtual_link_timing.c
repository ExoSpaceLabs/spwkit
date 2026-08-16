// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/virtual_link_timing.h"

uint64_t spw_virtual_link_serialization_us(
    const spw_virtual_link_timing_t* timing,
    spw_virtual_link_event_t event,
    size_t payload_size) {
    uint64_t effective_octets;
    uint64_t bit_microseconds;
    uint64_t whole;

    if (timing == NULL || timing->link_bps == 0u) {
        return 0u;
    }

    effective_octets = event == SPW_VIRTUAL_LINK_EVENT_DATA
                           ? (uint64_t)payload_size + 1u
                           : 2u;
    bit_microseconds = effective_octets * UINT64_C(8) * UINT64_C(1000000);
    whole = bit_microseconds / timing->link_bps;
    return whole + (bit_microseconds % timing->link_bps != 0u ? 1u : 0u);
}

uint64_t spw_virtual_link_delay_us(const spw_virtual_link_timing_t* timing,
                                   spw_virtual_link_event_t event,
                                   size_t payload_size) {
    if (timing == NULL) {
        return 0u;
    }
    return spw_virtual_link_serialization_us(timing, event, payload_size) +
           timing->latency_us;
}
