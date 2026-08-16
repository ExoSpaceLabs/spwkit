// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_DEVICE_H
#define SPWKIT_DEVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPW_DEVICE_CONFIG_VERSION 1u
#define SPW_DEVICE_ENDPOINT_CAPACITY 108u
#define SPW_DEVICE_DEFAULT_ENDPOINT "/tmp/spwkit-vspwd.sock"

typedef struct spw_device_config {
    uint32_t struct_size;
    uint32_t version;
    uint32_t port_id;
    uint32_t reserved;
    char endpoint[SPW_DEVICE_ENDPOINT_CAPACITY];
} spw_device_config_t;

#define SPW_DEVICE_CONFIG_INITIALIZER(port_id_) \
    { sizeof(spw_device_config_t), SPW_DEVICE_CONFIG_VERSION, \
      (uint32_t)(port_id_), 0u, SPW_DEVICE_DEFAULT_ENDPOINT }

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_DEVICE_H */
