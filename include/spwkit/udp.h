// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_UDP_H
#define SPWKIT_UDP_H

#include <stdint.h>

#include "spwkit/api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPW_UDP_CONFIG_VERSION 3u
#define SPW_UDP_ADDRESS_MAX 64u
#define SPW_UDP_DEFAULT_FRAGMENT_PAYLOAD 1200u
#define SPW_UDP_DEFAULT_ACK_TIMEOUT_MS 100u
#define SPW_UDP_DEFAULT_MAX_RETRIES 5u
#define SPW_UDP_DEFAULT_KEEPALIVE_INTERVAL_MS 1000u
#define SPW_UDP_DEFAULT_PEER_TIMEOUT_MS 3000u
#define SPW_UDP_DEFAULT_VIRTUAL_LINK_BPS 0ull
#define SPW_UDP_DEFAULT_VIRTUAL_LATENCY_US 0u
#define SPW_UDP_DEFAULT_FAULT_SEED 0x9e3779b97f4a7c15ull
#define SPW_UDP_FAULT_RULE_COUNT 8u
#define SPW_UDP_FAULT_PROBABILITY_SCALE 10000u

typedef uint8_t spw_udp_fault_action_t;
#define SPW_UDP_FAULT_ACTION_NONE                ((spw_udp_fault_action_t)0u)
#define SPW_UDP_FAULT_ACTION_TRANSPORT_DROP      ((spw_udp_fault_action_t)1u)
#define SPW_UDP_FAULT_ACTION_TRANSPORT_DUPLICATE ((spw_udp_fault_action_t)2u)
#define SPW_UDP_FAULT_ACTION_TRANSPORT_REORDER   ((spw_udp_fault_action_t)3u)
#define SPW_UDP_FAULT_ACTION_TRANSPORT_DELAY     ((spw_udp_fault_action_t)4u)
#define SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP       ((spw_udp_fault_action_t)5u)

typedef uint8_t spw_udp_fault_target_t;
#define SPW_UDP_FAULT_TARGET_ANY       ((spw_udp_fault_target_t)0u)
#define SPW_UDP_FAULT_TARGET_DATA      ((spw_udp_fault_target_t)1u)
#define SPW_UDP_FAULT_TARGET_TIME_CODE ((spw_udp_fault_target_t)2u)
#define SPW_UDP_FAULT_TARGET_ACK       ((spw_udp_fault_target_t)3u)
#define SPW_UDP_FAULT_TARGET_KEEPALIVE ((spw_udp_fault_target_t)4u)

/**
 * One deterministic fault-injection rule.
 *
 * Rules are evaluated in array order. For matching events the rule advances a
 * deterministic per-rule PRNG derived from `fault_seed`. A rule fires when the
 * generated value falls below `probability_per_10000`; 10000 therefore means
 * always. `max_events == 0` means unlimited firings, otherwise the rule becomes
 * inactive after that many injected events.
 *
 * Transport actions operate only on VSPW-TP carrier datagrams. SPACEWIRE_EEP
 * operates on the logical outgoing DATA packet before VSPW-TP framing and is
 * therefore intentionally application-visible at the remote SpaceWire API.
 */
typedef struct spw_udp_fault_rule {
    spw_udp_fault_action_t action;
    spw_udp_fault_target_t target;
    uint16_t probability_per_10000;
    uint32_t max_events;
    uint32_t delay_us;
    uint32_t reserved;
} spw_udp_fault_rule_t;

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
 *
 * Fault rules are disabled by default. They are fixed-size, copied with the
 * backend configuration, deterministic for a given seed/event stream, and do
 * not require allocation or a worker thread.
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
    uint64_t fault_seed;
    spw_udp_fault_rule_t fault_rules[SPW_UDP_FAULT_RULE_COUNT];
    uint32_t reserved;
} spw_udp_config_t;

#define SPW_UDP_FAULT_RULE_INITIALIZER \
    { SPW_UDP_FAULT_ACTION_NONE, SPW_UDP_FAULT_TARGET_ANY, 0u, 0u, 0u, 0u }

#define SPW_UDP_CONFIG_INITIALIZER(local_port_, remote_port_, link_id_) \
    { sizeof(spw_udp_config_t), SPW_UDP_CONFIG_VERSION, "127.0.0.1", \
      (uint16_t)(local_port_), "127.0.0.1", (uint16_t)(remote_port_), \
      (uint32_t)(link_id_), (uint16_t)SPW_UDP_DEFAULT_FRAGMENT_PAYLOAD, \
      (uint16_t)SPW_UDP_DEFAULT_MAX_RETRIES, \
      (uint32_t)SPW_UDP_DEFAULT_ACK_TIMEOUT_MS, \
      (uint32_t)SPW_UDP_DEFAULT_KEEPALIVE_INTERVAL_MS, \
      (uint32_t)SPW_UDP_DEFAULT_PEER_TIMEOUT_MS, \
      (uint64_t)SPW_UDP_DEFAULT_VIRTUAL_LINK_BPS, \
      (uint32_t)SPW_UDP_DEFAULT_VIRTUAL_LATENCY_US, \
      (uint64_t)SPW_UDP_DEFAULT_FAULT_SEED, \
      { SPW_UDP_FAULT_RULE_INITIALIZER, SPW_UDP_FAULT_RULE_INITIALIZER, \
        SPW_UDP_FAULT_RULE_INITIALIZER, SPW_UDP_FAULT_RULE_INITIALIZER, \
        SPW_UDP_FAULT_RULE_INITIALIZER, SPW_UDP_FAULT_RULE_INITIALIZER, \
        SPW_UDP_FAULT_RULE_INITIALIZER, SPW_UDP_FAULT_RULE_INITIALIZER }, \
      0u }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_UDP_H */
