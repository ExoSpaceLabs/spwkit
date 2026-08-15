// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_UDP_H
#define SPWKIT_UDP_H

#include <stdint.h>

#include "spwkit/api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPW_UDP_CONFIG_VERSION 1u
#define SPW_UDP_ADDRESS_MAX 64u
#define SPW_UDP_DEFAULT_FRAGMENT_PAYLOAD 1200u

/**
 * Configuration for the distributed VSPW-TP/UDP backend.
 *
 * Addresses are copied by libspwkit during open. IPv4 numeric addresses are
 * used in v0.2 so backend construction remains deterministic and does not
 * depend on DNS. Applications still communicate exclusively through the
 * normal spw_port_* API.
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
    uint16_t reserved;
} spw_udp_config_t;

#define SPW_UDP_CONFIG_INITIALIZER(local_port_, remote_port_, link_id_) \
    { sizeof(spw_udp_config_t), SPW_UDP_CONFIG_VERSION, "127.0.0.1", \
      (uint16_t)(local_port_), "127.0.0.1", (uint16_t)(remote_port_), \
      (uint32_t)(link_id_), (uint16_t)SPW_UDP_DEFAULT_FRAGMENT_PAYLOAD, 0u }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_UDP_H */
