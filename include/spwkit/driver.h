// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_DRIVER_H
#define SPWKIT_DRIVER_H

#include <stddef.h>
#include <stdint.h>

#include "spwkit/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* v1 was development-only; v2 adds the DMA ownership contract. */
#define SPW_DRIVER_OPS_VERSION 2u
#define SPW_DRIVER_CONFIG_VERSION 2u
#define SPW_DRIVER_MAX_BUFFER_SLOTS 64u

typedef uint64_t spw_driver_buffer_token_t;

typedef struct spw_driver_buffer {
    uint8_t* data;
    size_t length;
    size_t capacity;
    spw_terminator_t terminator;
    spw_driver_buffer_token_t token;
} spw_driver_buffer_t;

typedef uint32_t spw_driver_sync_direction_t;
#define SPW_DRIVER_SYNC_TO_DEVICE   ((spw_driver_sync_direction_t)1u)
#define SPW_DRIVER_SYNC_FROM_DEVICE ((spw_driver_sync_direction_t)2u)

/**
 * Portable hardware-driver callback table.
 *
 * The callback table and driver_context are owned by the caller/vendor
 * driver and must remain valid until the SpWKit port is closed. No POSIX,
 * Winsock, MMIO, physical-address, AXI, DMA-descriptor or interrupt handle
 * type appears in this ABI.
 *
 * Lifecycle, copied packet I/O, link state and capabilities are required.
 * Time-code, statistics and readiness callbacks are optional and must agree
 * with the capability bits returned by get_capabilities().
 *
 * DMA/zero-copy support is optional but atomic: when any DMA ownership
 * callback is supplied, all six ownership callbacks must be supplied.
 */
typedef struct spw_driver_ops {
    uint32_t struct_size;
    uint32_t version;

    spw_result_t (*start)(void* driver_context);
    spw_result_t (*stop)(void* driver_context);
    spw_result_t (*reset)(void* driver_context);

    spw_result_t (*get_link_state)(const void* driver_context,
                                   spw_link_state_t* out_state);
    spw_result_t (*get_capabilities)(const void* driver_context,
                                     spw_capabilities_t* out_capabilities);

    spw_result_t (*send)(void* driver_context,
                         const spw_packet_t* packet,
                         spw_timeout_us_t timeout_us);
    spw_result_t (*receive)(void* driver_context,
                            spw_packet_t* packet,
                            spw_timeout_us_t timeout_us);

    spw_result_t (*send_time_code)(void* driver_context,
                                   const spw_time_code_t* time_code,
                                   spw_timeout_us_t timeout_us);
    spw_result_t (*receive_time_code)(void* driver_context,
                                      spw_time_code_t* time_code,
                                      spw_timeout_us_t timeout_us);

    spw_result_t (*get_statistics)(const void* driver_context,
                                   spw_statistics_t* out_statistics);
    spw_result_t (*clear_statistics)(void* driver_context);

    /** Optional level-triggered, non-consuming receive readiness. */
    spw_result_t (*wait)(void* driver_context,
                         spw_ready_events_t interests,
                         spw_timeout_us_t timeout_us,
                         spw_ready_events_t* out_ready);

    spw_result_t (*acquire_tx_buffer)(
        void* driver_context,
        size_t min_capacity,
        spw_timeout_us_t timeout_us,
        spw_driver_buffer_t* out_buffer);
    spw_result_t (*submit_tx_buffer)(
        void* driver_context,
        const spw_driver_buffer_t* buffer,
        spw_timeout_us_t timeout_us);
    spw_result_t (*reclaim_tx_buffer)(
        void* driver_context,
        spw_timeout_us_t timeout_us,
        spw_driver_buffer_t* out_buffer);
    spw_result_t (*release_tx_buffer)(
        void* driver_context,
        const spw_driver_buffer_t* buffer);
    spw_result_t (*acquire_rx_buffer)(
        void* driver_context,
        spw_timeout_us_t timeout_us,
        spw_driver_buffer_t* out_buffer);
    spw_result_t (*release_rx_buffer)(
        void* driver_context,
        const spw_driver_buffer_t* buffer);

    /**
     * Optional cache/coherency synchronization hook.
     *
     * TO_DEVICE runs immediately before TX ownership is submitted to the
     * driver. FROM_DEVICE runs after RX ownership is acquired and before
     * application access. NULL means the memory is coherent or the vendor
     * ownership callbacks already perform the required maintenance.
     */
    spw_result_t (*sync_buffer)(
        void* driver_context,
        const spw_driver_buffer_t* buffer,
        spw_driver_sync_direction_t direction);
} spw_driver_ops_t;

typedef struct spw_driver_config {
    uint32_t struct_size;
    uint32_t version;
    const spw_driver_ops_t* ops;
    void* driver_context;
    uint32_t reserved;
    size_t tx_buffer_slots;
    size_t rx_buffer_slots;
} spw_driver_config_t;

#define SPW_DRIVER_OPS_INITIALIZER \
    { sizeof(spw_driver_ops_t), SPW_DRIVER_OPS_VERSION }

#define SPW_DRIVER_CONFIG_INITIALIZER(ops_, context_) \
    { sizeof(spw_driver_config_t), SPW_DRIVER_CONFIG_VERSION, \
      (ops_), (context_), 0u, 0u, 0u }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_DRIVER_H */
