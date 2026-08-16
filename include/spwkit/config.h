// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_CONFIG_H
#define SPWKIT_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#include "spwkit/api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SPW_PORT_CONFIG_VERSION 1u

typedef uint32_t spw_backend_id_t;
#define SPW_BACKEND_LOOPBACK  ((spw_backend_id_t)1u)
#define SPW_BACKEND_SIMULATOR ((spw_backend_id_t)2u)
#define SPW_BACKEND_UDP       ((spw_backend_id_t)3u)
#define SPW_BACKEND_DEVICE    ((spw_backend_id_t)4u)

/**
 * Backend-independent port configuration.
 *
 * backend_config is interpreted only by the selected backend. libspwkit reads
 * it during spw_port_open(); callers may release or reuse that configuration
 * storage after open returns unless a backend-specific contract states
 * otherwise.
 */
struct spw_port_config {
    uint32_t struct_size;
    uint32_t version;
    spw_backend_id_t backend;
    uint32_t flags;
    const void* backend_config;
    size_t backend_config_size;
};

#define SPW_PORT_CONFIG_INITIALIZER(backend_id_) \
    { sizeof(spw_port_config_t), SPW_PORT_CONFIG_VERSION, \
      (spw_backend_id_t)(backend_id_), 0u, NULL, 0u }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_CONFIG_H */
