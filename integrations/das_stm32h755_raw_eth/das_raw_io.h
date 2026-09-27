// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_DAS_RAW_IO_H
#define SPWKIT_DAS_RAW_IO_H

#include <das/eth.h>
#include <spwkit/raw_ethernet.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct spw_das_raw_io {
    das_eth_t eth;
    bool started;
    bool pending_valid;
    size_t pending_size;
    uint8_t pending_frame[DAS_ETH_MAX_FRAME_SIZE];
} spw_das_raw_io_t;

void spw_das_raw_io_init(spw_das_raw_io_t* context, das_eth_t eth);

extern const spw_raw_ethernet_io_ops_t SPW_DAS_RAW_ETHERNET_IO_OPS;
extern const spw_runtime_ops_t SPW_DAS_RUNTIME_OPS;

#endif
