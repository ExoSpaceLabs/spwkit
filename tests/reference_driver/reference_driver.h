// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_TEST_REFERENCE_DRIVER_H
#define SPWKIT_TEST_REFERENCE_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <spwkit/driver.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SPW_REFERENCE_PACKET_CAPACITY = 4096,
    SPW_REFERENCE_QUEUE_DEPTH = 4,
    SPW_REFERENCE_DMA_SLOTS = 2
};

typedef struct spw_reference_packet_slot {
    uint8_t bytes[SPW_REFERENCE_PACKET_CAPACITY];
    size_t length;
    spw_terminator_t terminator;
} spw_reference_packet_slot_t;

typedef enum spw_reference_dma_state {
    SPW_REFERENCE_DMA_FREE = 0,
    SPW_REFERENCE_DMA_TX_APP,
    SPW_REFERENCE_DMA_TX_COMPLETE,
    SPW_REFERENCE_DMA_RX_READY,
    SPW_REFERENCE_DMA_RX_APP
} spw_reference_dma_state_t;

typedef struct spw_reference_dma_slot {
    uint8_t bytes[SPW_REFERENCE_PACKET_CAPACITY];
    size_t length;
    spw_terminator_t terminator;
    spw_driver_buffer_token_t token;
    spw_reference_dma_state_t state;
} spw_reference_dma_slot_t;

struct spw_reference_endpoint;
typedef struct spw_reference_endpoint spw_reference_endpoint_t;

struct spw_reference_endpoint {
    spw_link_state_t state;
    unsigned endpoint_index;
    spw_reference_endpoint_t* peer;

    spw_reference_packet_slot_t rx_packets[SPW_REFERENCE_QUEUE_DEPTH];
    size_t rx_packet_head;
    size_t rx_packet_count;

    spw_time_code_t rx_time_codes[SPW_REFERENCE_QUEUE_DEPTH];
    size_t rx_time_code_head;
    size_t rx_time_code_count;

    spw_reference_dma_slot_t tx_dma[SPW_REFERENCE_DMA_SLOTS];
    spw_reference_dma_slot_t rx_dma[SPW_REFERENCE_DMA_SLOTS];

    spw_statistics_t statistics;
    uint32_t sync_to_device_count;
    uint32_t sync_from_device_count;
};

typedef struct spw_reference_link {
    spw_reference_endpoint_t endpoints[2];
} spw_reference_link_t;

/** Initialize a deterministic two-endpoint, fixed-storage reference link. */
void spw_reference_link_init(spw_reference_link_t* link);

/** Return one endpoint context suitable for spw_driver_config_t::driver_context. */
spw_reference_endpoint_t* spw_reference_link_endpoint(
    spw_reference_link_t* link,
    unsigned endpoint_index);

/** Portable driver callback table implemented by the reference link. */
const spw_driver_ops_t* spw_reference_driver_ops(void);

/** Test evidence helpers used to prove application views map driver-owned DMA memory. */
bool spw_reference_owns_tx_pointer(const spw_reference_endpoint_t* endpoint,
                                   const uint8_t* pointer);
bool spw_reference_owns_rx_pointer(const spw_reference_endpoint_t* endpoint,
                                   const uint8_t* pointer);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_TEST_REFERENCE_DRIVER_H */
