// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_VSPD_MANAGEMENT_CLIENT_H
#define SPWKIT_VSPD_MANAGEMENT_CLIENT_H

#include "backends/device/vspw_device_protocol.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VSPD_MANAGEMENT_EVENT_QUEUE_DEPTH 8u

typedef struct vspd_management_event {
    uint32_t port_id;
    vspd_port_snapshot_payload_t snapshot;
} vspd_management_event_t;

typedef struct vspd_management_client {
    int fd;
    uint32_t next_request_id;
    uint32_t event_head;
    uint32_t event_tail;
    uint32_t event_count;
    vspd_management_event_t events[VSPD_MANAGEMENT_EVENT_QUEUE_DEPTH];
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
int32_t vspd_management_subscribe_port(
    vspd_management_client_t* client,
    uint32_t port_id);
int32_t vspd_management_unsubscribe_port(
    vspd_management_client_t* client,
    uint32_t port_id);
int32_t vspd_management_receive_snapshot(
    vspd_management_client_t* client,
    int timeout_ms,
    uint32_t* out_port_id,
    vspd_port_snapshot_payload_t* out_snapshot);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_VSPD_MANAGEMENT_CLIENT_H */
