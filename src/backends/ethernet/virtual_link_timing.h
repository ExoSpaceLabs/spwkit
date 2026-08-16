// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_VIRTUAL_LINK_TIMING_H
#define SPWKIT_VIRTUAL_LINK_TIMING_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t spw_virtual_link_event_t;
enum {
    SPW_VIRTUAL_LINK_EVENT_DATA = 0u,
    SPW_VIRTUAL_LINK_EVENT_TIME_CODE = 1u
};

typedef struct spw_virtual_link_timing {
    uint64_t link_bps;
    uint32_t latency_us;
} spw_virtual_link_timing_t;

uint64_t spw_virtual_link_serialization_us(
    const spw_virtual_link_timing_t* timing,
    spw_virtual_link_event_t event,
    size_t payload_size);
uint64_t spw_virtual_link_delay_us(const spw_virtual_link_timing_t* timing,
                                   spw_virtual_link_event_t event,
                                   size_t payload_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_VIRTUAL_LINK_TIMING_H */
