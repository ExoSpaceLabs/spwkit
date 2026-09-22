// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_UDP_TRANSPORT_PROVIDER_H
#define SPWKIT_UDP_TRANSPORT_PROVIDER_H

#include "backends/ethernet/transport_provider.h"

#include <stdbool.h>
#include <stdint.h>

#include <spwkit/udp.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sockaddr_in;

typedef struct spw_udp_transport {
    int socket_fd;
    bool started;
    struct sockaddr_in* local_address_storage;
    struct sockaddr_in* remote_address_storage;
    spw_transport_peer_id_t remote_peer;
} spw_udp_transport_t;

spw_result_t spw_udp_transport_init(spw_udp_transport_t* transport,
                                    const spw_udp_config_t* config);
void spw_udp_transport_destroy(spw_udp_transport_t* transport);
void spw_udp_transport_provider(spw_udp_transport_t* transport,
                                spw_transport_provider_t* out_provider);
spw_result_t spw_udp_transport_remote_peer(
    const spw_udp_transport_t* transport,
    spw_transport_peer_id_t* out_peer);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_UDP_TRANSPORT_PROVIDER_H */
