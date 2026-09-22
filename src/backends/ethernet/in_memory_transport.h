// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_IN_MEMORY_TRANSPORT_H
#define SPWKIT_IN_MEMORY_TRANSPORT_H

#include "backends/ethernet/transport_provider.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SPW_IN_MEMORY_TRANSPORT_ENDPOINTS = 2u,
    SPW_IN_MEMORY_TRANSPORT_QUEUE_DEPTH = 8u,
    SPW_IN_MEMORY_TRANSPORT_MAX_MESSAGE_SIZE = 2048u
};

typedef struct spw_in_memory_transport_message {
    uint8_t data[SPW_IN_MEMORY_TRANSPORT_MAX_MESSAGE_SIZE];
    size_t size;
    spw_transport_peer_id_t source;
} spw_in_memory_transport_message_t;

typedef struct spw_in_memory_transport_queue {
    spw_in_memory_transport_message_t messages[SPW_IN_MEMORY_TRANSPORT_QUEUE_DEPTH];
    size_t head;
    size_t count;
} spw_in_memory_transport_queue_t;

typedef struct spw_in_memory_transport_link {
    spw_in_memory_transport_queue_t inbox[SPW_IN_MEMORY_TRANSPORT_ENDPOINTS];
} spw_in_memory_transport_link_t;

typedef struct spw_in_memory_transport_endpoint {
    spw_in_memory_transport_link_t* link;
    size_t endpoint_index;
    bool started;
    spw_transport_peer_id_t local_peer;
    spw_transport_peer_id_t remote_peer;
} spw_in_memory_transport_endpoint_t;

void spw_in_memory_transport_link_init(spw_in_memory_transport_link_t* link);
spw_result_t spw_in_memory_transport_endpoint_init(
    spw_in_memory_transport_endpoint_t* endpoint,
    spw_in_memory_transport_link_t* link,
    size_t endpoint_index,
    const spw_transport_peer_id_t* local_peer,
    const spw_transport_peer_id_t* remote_peer);
void spw_in_memory_transport_provider(
    spw_in_memory_transport_endpoint_t* endpoint,
    spw_transport_provider_t* out_provider);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_IN_MEMORY_TRANSPORT_H */
