// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_RAW_ETHERNET_TRANSPORT_PROVIDER_H
#define SPWKIT_RAW_ETHERNET_TRANSPORT_PROVIDER_H

#include "backends/ethernet/transport_provider.h"
#include "backends/ethernet/vspw_runtime.h"

#include <spwkit/raw_ethernet.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SPW_RAW_ETHERNET_HEADER_SIZE = 14u,
    SPW_RAW_ETHERNET_PROTOCOL_HEADER_SIZE = 6u,
    SPW_RAW_ETHERNET_CARRIER_OVERHEAD =
        SPW_RAW_ETHERNET_HEADER_SIZE + SPW_RAW_ETHERNET_PROTOCOL_HEADER_SIZE
};

typedef struct spw_raw_ethernet_transport {
    spw_raw_ethernet_config_t config;
    spw_vspw_runtime_t runtime;
    spw_transport_peer_id_t remote_peer;
    size_t max_frame_size;
    bool started;
    uint8_t tx_frame[SPW_RAW_ETHERNET_MAX_FRAME_SIZE];
    uint8_t rx_frame[SPW_RAW_ETHERNET_MAX_FRAME_SIZE];
} spw_raw_ethernet_transport_t;

spw_result_t spw_raw_ethernet_transport_init(
    spw_raw_ethernet_transport_t* transport,
    const spw_raw_ethernet_config_t* config,
    const spw_vspw_runtime_t* runtime);
void spw_raw_ethernet_transport_provider(
    spw_raw_ethernet_transport_t* transport,
    spw_transport_provider_t* out_provider);
spw_result_t spw_raw_ethernet_transport_remote_peer(
    const spw_raw_ethernet_transport_t* transport,
    spw_transport_peer_id_t* out_peer);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_RAW_ETHERNET_TRANSPORT_PROVIDER_H */
