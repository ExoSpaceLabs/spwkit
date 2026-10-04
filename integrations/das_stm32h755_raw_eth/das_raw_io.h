// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_DAS_RAW_IO_H
#define SPWKIT_DAS_RAW_IO_H

#include <das/eth.h>
#include <spwkit/raw_ethernet.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct spw_das_cycle_stats {
    uint32_t count;
    uint32_t min_cycles;
    uint32_t max_cycles;
    uint64_t total_cycles;
} spw_das_cycle_stats_t;

typedef struct spw_das_raw_io_stats {
    spw_das_cycle_stats_t tx_send;
    spw_das_cycle_stats_t rx_poll;
    spw_das_cycle_stats_t rx_success;
    uint32_t tx_successes;
    uint32_t tx_failures;
    uint32_t rx_errors;
    uint32_t rx_empty_polls;
    uint32_t last_tx_size;
    uint32_t last_rx_size;
    uint8_t last_tx_header[14];
    uint8_t last_rx_header[14];
} spw_das_raw_io_stats_t;

typedef struct spw_das_raw_io {
    das_eth_t eth;
    bool started;
    bool pending_valid;
    size_t pending_size;
    uint8_t pending_frame[DAS_ETH_MAX_FRAME_SIZE];
    spw_das_raw_io_stats_t stats;
} spw_das_raw_io_t;

void spw_das_raw_io_init(spw_das_raw_io_t* context, das_eth_t eth);
void spw_das_raw_io_get_stats(const spw_das_raw_io_t* context,
                              spw_das_raw_io_stats_t* out_stats);
uint32_t spw_das_cycle_counter_read(void);

extern const spw_raw_ethernet_io_ops_t SPW_DAS_RAW_ETHERNET_IO_OPS;
extern const spw_runtime_ops_t SPW_DAS_RUNTIME_OPS;

#endif
