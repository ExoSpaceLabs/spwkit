// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_TYPES_H
#define SPWKIT_TYPES_H

#include <stddef.h>
#include <stdint.h>

#include "spwkit/api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Result codes. Zero is success; errors are negative. */
#define SPW_OK                         ((spw_result_t)0)
#define SPW_ERR_INVALID_ARGUMENT       ((spw_result_t)-1)
#define SPW_ERR_INVALID_STATE          ((spw_result_t)-2)
#define SPW_ERR_TIMEOUT                ((spw_result_t)-3)
#define SPW_ERR_UNSUPPORTED            ((spw_result_t)-4)
#define SPW_ERR_RESOURCE_EXHAUSTED     ((spw_result_t)-5)
#define SPW_ERR_LINK_UNAVAILABLE       ((spw_result_t)-6)
#define SPW_ERR_BUFFER_TOO_SMALL       ((spw_result_t)-7)
#define SPW_ERR_INVALID_PACKET         ((spw_result_t)-8)
#define SPW_ERR_BACKEND                ((spw_result_t)-9)

#define SPW_TIMEOUT_IMMEDIATE ((spw_timeout_us_t)0u)
#define SPW_TIMEOUT_INFINITE  ((spw_timeout_us_t)UINT64_MAX)

/* SpaceWire packet terminator. */
typedef uint8_t spw_terminator_t;
#define SPW_TERMINATOR_EOP ((spw_terminator_t)0u)
#define SPW_TERMINATOR_EEP ((spw_terminator_t)1u)

/* ECSS-oriented SpaceWire exchange/link states. */
typedef uint8_t spw_link_state_t;
#define SPW_LINK_ERROR_RESET ((spw_link_state_t)0u)
#define SPW_LINK_ERROR_WAIT  ((spw_link_state_t)1u)
#define SPW_LINK_READY       ((spw_link_state_t)2u)
#define SPW_LINK_STARTED     ((spw_link_state_t)3u)
#define SPW_LINK_CONNECTING  ((spw_link_state_t)4u)
#define SPW_LINK_RUN         ((spw_link_state_t)5u)

/* Optional backend capabilities. */
typedef uint64_t spw_capability_bits_t;
#define SPW_CAP_NONE              ((spw_capability_bits_t)0u)
#define SPW_CAP_EEP               ((spw_capability_bits_t)(1ull << 0))
#define SPW_CAP_TIME_CODE         ((spw_capability_bits_t)(1ull << 1))
#define SPW_CAP_LINK_CONTROL      ((spw_capability_bits_t)(1ull << 2))
#define SPW_CAP_STATISTICS        ((spw_capability_bits_t)(1ull << 3))
#define SPW_CAP_RATE_CONTROL      ((spw_capability_bits_t)(1ull << 4))
#define SPW_CAP_FAULT_INJECTION   ((spw_capability_bits_t)(1ull << 5))
#define SPW_CAP_ZERO_COPY         ((spw_capability_bits_t)(1ull << 6))
#define SPW_CAP_READINESS         ((spw_capability_bits_t)(1ull << 7))

/* Backend-neutral receive-readiness interests/results. */
typedef uint32_t spw_ready_events_t;
#define SPW_READY_NONE         ((spw_ready_events_t)0u)
#define SPW_READY_RX_PACKET    ((spw_ready_events_t)(1u << 0))
#define SPW_READY_RX_TIME_CODE ((spw_ready_events_t)(1u << 1))
#define SPW_READY_ALL          (SPW_READY_RX_PACKET | SPW_READY_RX_TIME_CODE)

struct spw_capabilities {
    spw_capability_bits_t bits;
    size_t max_packet_size;
    size_t tx_queue_depth;
    size_t rx_queue_depth;
    size_t buffer_alignment;
};

/*
 * Complete software-visible SpaceWire packet for copied I/O.
 *
 * TX: data points to caller-owned bytes; length is the payload length.
 * RX: data points to caller-provided writable storage; capacity is available
 *     storage and length is set to the received/required payload length.
 */
struct spw_packet {
    uint8_t* data;
    size_t length;
    size_t capacity;
    spw_terminator_t terminator;
};

/*
 * Application-visible view of an opaque zero-copy buffer.
 *
 * TX buffers are writable while application-owned. RX buffers are read-only by
 * contract even though C represents the pointer as uint8_t* for one common ABI
 * shape. The pointer remains valid only while the application owns the opaque
 * spw_buffer_t handle.
 */
struct spw_buffer_view {
    uint8_t* data;
    size_t length;
    size_t capacity;
    spw_terminator_t terminator;
};

/*
 * SpaceWire time-code representation.
 * time_count uses the least-significant six-bit value range 0..63.
 * control_flags holds the two control bits. For ordinary time-codes v0.1
 * expects control_flags == 0; broader broadcast-code semantics are reserved.
 */
struct spw_time_code {
    uint8_t time_count;
    uint8_t control_flags;
};

struct spw_statistics {
    uint64_t tx_packets;
    uint64_t rx_packets;
    uint64_t tx_bytes;
    uint64_t rx_bytes;
    uint64_t tx_time_codes;
    uint64_t rx_time_codes;
    uint64_t eep_packets;
    uint64_t link_errors;
    uint64_t dropped_packets;
};

/*
 * Optional simulation/fault diagnostics. Backends without fault injection
 * return SPW_ERR_UNSUPPORTED from the corresponding port operations.
 *
 * Transport counters describe VSPW-TP carrier manipulation. SpaceWire
 * counters describe faults intentionally made visible through the logical
 * SpaceWire API. Keeping these domains separate prevents network faults from
 * being mistaken for simulated SpaceWire errors.
 */
struct spw_fault_statistics {
    uint64_t transport_drops;
    uint64_t transport_duplicates;
    uint64_t transport_reorders;
    uint64_t transport_delays;
    uint64_t spacewire_eep_injections;
};

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_TYPES_H */
