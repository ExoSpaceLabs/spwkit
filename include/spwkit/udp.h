// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_UDP_H
#define SPWKIT_UDP_H

#include <stdint.h>

#include "spwkit/api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPW_UDP_CONFIG_VERSION 2u
#define SPW_UDP_ADDRESS_MAX 64u
#define SPW_UDP_DEFAULT_FRAGMENT_PAYLOAD 1200u
#define SPW_UDP_DEFAULT_ACK_TIMEOUT_MS 100u
#define SPW_UDP_DEFAULT_MAX_RETRIES 5u
#define SPW_UDP_DEFAULT_KEEPALIVE_INTERVAL_MS 1000u
#define SPW_UDP_DEFAULT_PEER_TIMEOUT_MS 3000u
#define SPW_UDP_DEFAULT_VIRTUAL_LINK_BPS 0ull
#define SPW_UDP_DEFAULT_VIRTUAL_LATENCY_US 0u

/**
 * Configuration for the distributed VSPW-TP/UDP backend.
 *
 * Addresses are copied by libspwkit during open. IPv4 numeric addresses are
 * used in v0.2 so backend construction remains deterministic and does not
 * depend on DNS. Applications still communicate exclusively through the
 * normal spw_port_* API.
 *
 * Reliability is transport-level and cooperative: libspwkit retains at most
 * one unacknowledged logical outbound event, retransmits it while API calls
 * service the backend, and uses keepalives to detect/recover peer liveness.
 *
 * Optional virtual-link timing is SpaceWire-side simulation state, not UDP
 * transport timing. A zero bit rate disables serialization delay and a zero
 * latency disables the fixed propagation/processing delay.
 */
typedef struct spw_udp_config {
    uint32_t struct_size;
    uint32_t version;
    char local_address[SPW_UDP_ADDRESS_MAX];
    uint16_t local_port;
    char remote_address[SPW_UDP_ADDRESS_MAX];
    uint16_t remote_port;
    uint32_t link_id;
    uint16_t fragment_payload_size;
    uint16_t max_retries;
    uint32_t ack_timeout_ms;
    uint32_t keepalive_interval_ms;
    uint32_t peer_timeout_ms;
    uint64_t virtual_link_bps;
    uint32_t virtual_latency_us;
    uint32_t reserved;
} spw_udp_config_t;

#define SPW_UDP_CONFIG_INITIALIZER(local_port_, remote_port_, link_id_) \
    { sizeof(spw_udp_config_t), SPW_UDP_CONFIG_VERSION, "127.0.0.1", \
      (uint16_t)(local_port_), "127.0.0.1", (uint16_t)(remote_port_), \
      (uint32_t)(link_id_), (uint16_t)SPW_UDP_DEFAULT_FRAGMENT_PAYLOAD, \
      (uint16_t)SPW_UDP_DEFAULT_MAX_RETRIES, \
      (uint32_t)SPW_UDP_DEFAULT_ACK_TIMEOUT_MS, \
      (uint32_t)SPW_UDP_DEFAULT_KEEPALIVE_INTERVAL_MS, \
      (uint32_t)SPW_UDP_DEFAULT_PEER_TIMEOUT_MS, \
      (uint64_t)SPW_UDP_DEFAULT_VIRTUAL_LINK_BPS, \
      (uint32_t)SPW_UDP_DEFAULT_VIRTUAL_LATENCY_US, 0u }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_UDP_H */
