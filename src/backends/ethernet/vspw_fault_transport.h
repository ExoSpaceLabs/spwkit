// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_VSPW_FAULT_TRANSPORT_H
#define SPWKIT_VSPW_FAULT_TRANSPORT_H

#include "backends/ethernet/deterministic_faults.h"
#include "backends/ethernet/transport_provider.h"
#include "backends/ethernet/vspw_runtime.h"
#include "backends/ethernet/vspw_tp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <spwkit/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spw_vspw_fault_transport {
    spw_transport_provider_t underlying;
    spw_vspw_runtime_t runtime;
    spw_deterministic_fault_injector_t* injector;
    spw_statistics_t* statistics;
    spw_fault_statistics_t* fault_statistics;
    uint8_t reordered_frame[SPW_VSPW_TP_MAX_CARRIER_FRAME];
    size_t reordered_size;
    spw_transport_peer_id_t reordered_peer;
    bool reordered_valid;
} spw_vspw_fault_transport_t;

spw_result_t spw_vspw_fault_transport_init(
    spw_vspw_fault_transport_t* transport,
    const spw_transport_provider_t* underlying,
    const spw_vspw_runtime_t* runtime,
    spw_deterministic_fault_injector_t* injector,
    spw_statistics_t* statistics,
    spw_fault_statistics_t* fault_statistics);
void spw_vspw_fault_transport_provider(
    spw_vspw_fault_transport_t* transport,
    spw_transport_provider_t* out_provider);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_VSPW_FAULT_TRANSPORT_H */
