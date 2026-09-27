// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_DAS_RAW_IO_H
#define SPWKIT_DAS_RAW_IO_H

#include <das/eth.h>
#include <spwkit/raw_ethernet.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t (*spw_das_cycle_read_fn)(void* context);

typedef struct spw_das_raw_io_metrics {
    uint64_t tx_das_cycles;
    uint64_t rx_ready_das_cycles;
    uint32_t tx_das_calls;
    uint32_t rx_ready_das_calls;
    uint32_t last_rx_ready_cycle;
} spw_das_raw_io_metrics_t;

typedef struct spw_das_raw_io {
    das_eth_t eth;
    bool started;
    bool pending_valid;
    size_t pending_size;
    uint8_t pending_frame[DAS_ETH_MAX_FRAME_SIZE];
    spw_das_cycle_read_fn cycle_read;
    void* cycle_context;
    spw_das_raw_io_metrics_t metrics;
} spw_das_raw_io_t;

void spw_das_raw_io_init(spw_das_raw_io_t* context, das_eth_t eth);
void spw_das_raw_io_set_cycle_counter(spw_das_raw_io_t* context,
                                      spw_das_cycle_read_fn read_cycles,
                                      void* cycle_context);
void spw_das_raw_io_get_metrics(const spw_das_raw_io_t* context,
                                spw_das_raw_io_metrics_t* out_metrics);

extern const spw_raw_ethernet_io_ops_t SPW_DAS_RAW_ETHERNET_IO_OPS;
extern const spw_runtime_ops_t SPW_DAS_RUNTIME_OPS;

#endif
