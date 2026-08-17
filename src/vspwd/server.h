// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_VSPWD_SERVER_H
#define SPWKIT_VSPWD_SERVER_H

#include <stdbool.h>
#include <stdint.h>

#include <spwkit/udp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VSPWD_DEFAULT_SOCKET_PATH "/tmp/spwkit-vspwd.sock"

typedef struct vspwd_udp_bridge_config {
    bool enabled;
    uint32_t port_id;
    spw_udp_config_t udp;
} vspwd_udp_bridge_config_t;

typedef struct vspwd_config {
    const char* socket_path;
    vspwd_udp_bridge_config_t udp_bridge;
} vspwd_config_t;

#define VSPWD_UDP_BRIDGE_CONFIG_INITIALIZER \
    { false, 0u, SPW_UDP_CONFIG_INITIALIZER(0u, 0u, 42u) }
#define VSPWD_CONFIG_INITIALIZER \
    { VSPWD_DEFAULT_SOCKET_PATH, VSPWD_UDP_BRIDGE_CONFIG_INITIALIZER }

/* Run until SIGINT/SIGTERM or a fatal server error. Returns 0 on clean stop. */
int vspwd_run_config(const vspwd_config_t* config);
int vspwd_run(const char* socket_path);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_VSPWD_SERVER_H */
