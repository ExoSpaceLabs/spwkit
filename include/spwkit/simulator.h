// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_SIMULATOR_H
#define SPWKIT_SIMULATOR_H

#include <stdint.h>

#include "spwkit/config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPW_SIMULATOR_CONFIG_VERSION 1u

typedef uint8_t spw_simulator_endpoint_t;
#define SPW_SIMULATOR_ENDPOINT_A ((spw_simulator_endpoint_t)0u)
#define SPW_SIMULATOR_ENDPOINT_B ((spw_simulator_endpoint_t)1u)

/**
 * Configuration for a local virtual SpaceWire endpoint.
 *
 * Two peers belong to the same virtual link when link_id matches and their
 * endpoint values are A and B respectively. Neither endpoint is a server or
 * client; both are equal SpaceWire peers.
 */
typedef struct spw_simulator_config {
    uint32_t struct_size;
    uint32_t version;
    uint64_t link_id;
    spw_simulator_endpoint_t endpoint;
    uint8_t reserved[7];
} spw_simulator_config_t;

#define SPW_SIMULATOR_CONFIG_INITIALIZER \
    { sizeof(spw_simulator_config_t), SPW_SIMULATOR_CONFIG_VERSION, 0u, \
      SPW_SIMULATOR_ENDPOINT_A, {0u, 0u, 0u, 0u, 0u, 0u, 0u} }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_SIMULATOR_H */
