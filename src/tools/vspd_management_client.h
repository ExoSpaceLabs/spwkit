// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_VSPD_MANAGEMENT_CLIENT_H
#define SPWKIT_VSPD_MANAGEMENT_CLIENT_H

#include "backends/device/vspw_device_protocol.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vspd_management_client {
    int fd;
    uint32_t next_request_id;
} vspd_management_client_t;

int32_t vspd_management_open(vspd_management_client_t* client,
                             const char* socket_path);
void vspd_management_close(vspd_management_client_t* client);

int32_t vspd_management_get_server_info(
    vspd_management_client_t* client,
    vspd_server_info_payload_t* out_info);
int32_t vspd_management_get_port_info(
    vspd_management_client_t* client,
    uint32_t port_id,
    vspd_port_info_payload_t* out_info);
int32_t vspd_management_get_port_statistics(
    vspd_management_client_t* client,
    uint32_t port_id,
    vspd_statistics_payload_t* out_statistics);
int32_t vspd_management_clear_port_statistics(
    vspd_management_client_t* client,
    uint32_t port_id);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_VSPD_MANAGEMENT_CLIENT_H */
