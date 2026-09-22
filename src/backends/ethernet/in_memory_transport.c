// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/in_memory_transport.h"

#include <string.h>

static size_t remote_index(const spw_in_memory_transport_endpoint_t* endpoint) {
    return endpoint->endpoint_index == 0u ? 1u : 0u;
}

static void clear_queue(spw_in_memory_transport_queue_t* queue) {
    queue->head = 0u;
    queue->count = 0u;
}

static spw_result_t memory_start(void* context) {
    spw_in_memory_transport_endpoint_t* endpoint =
        (spw_in_memory_transport_endpoint_t*)context;
    if (endpoint == NULL || endpoint->link == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    endpoint->started = true;
    return SPW_OK;
}

static spw_result_t memory_stop(void* context) {
    spw_in_memory_transport_endpoint_t* endpoint =
        (spw_in_memory_transport_endpoint_t*)context;
    if (endpoint == NULL || endpoint->link == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    endpoint->started = false;
    return SPW_OK;
}

static spw_result_t memory_reset(void* context) {
    spw_in_memory_transport_endpoint_t* endpoint =
        (spw_in_memory_transport_endpoint_t*)context;
    if (endpoint == NULL || endpoint->link == NULL ||
        endpoint->endpoint_index >= SPW_IN_MEMORY_TRANSPORT_ENDPOINTS) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    clear_queue(&endpoint->link->inbox[endpoint->endpoint_index]);
    return SPW_OK;
}

static spw_result_t memory_send(void* context,
                                const spw_transport_peer_id_t* peer,
                                const uint8_t* message,
                                size_t message_size,
                                spw_timeout_us_t timeout_us) {
    spw_in_memory_transport_endpoint_t* endpoint =
        (spw_in_memory_transport_endpoint_t*)context;
    spw_in_memory_transport_queue_t* queue;
    spw_in_memory_transport_message_t* slot;
    size_t index;
    (void)timeout_us;

    if (endpoint == NULL || endpoint->link == NULL || peer == NULL ||
        endpoint->endpoint_index >= SPW_IN_MEMORY_TRANSPORT_ENDPOINTS ||
        (message_size != 0u && message == NULL)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!endpoint->started) {
        return SPW_ERR_INVALID_STATE;
    }
    if (!spw_transport_peer_id_equal(peer, &endpoint->remote_peer)) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    if (message_size > SPW_IN_MEMORY_TRANSPORT_MAX_MESSAGE_SIZE) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }

    queue = &endpoint->link->inbox[remote_index(endpoint)];
    if (queue->count == SPW_IN_MEMORY_TRANSPORT_QUEUE_DEPTH) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    index = (queue->head + queue->count) % SPW_IN_MEMORY_TRANSPORT_QUEUE_DEPTH;
    slot = &queue->messages[index];
    if (message_size != 0u) {
        memcpy(slot->data, message, message_size);
    }
    slot->size = message_size;
    slot->source = endpoint->local_peer;
    ++queue->count;
    return SPW_OK;
}

static spw_result_t memory_receive(void* context,
                                   uint8_t* message,
                                   size_t message_capacity,
                                   size_t* out_message_size,
                                   spw_transport_peer_id_t* out_peer,
                                   spw_timeout_us_t timeout_us) {
    spw_in_memory_transport_endpoint_t* endpoint =
        (spw_in_memory_transport_endpoint_t*)context;
    spw_in_memory_transport_queue_t* queue;
    spw_in_memory_transport_message_t* slot;
    (void)timeout_us;

    if (endpoint == NULL || endpoint->link == NULL || out_message_size == NULL ||
        out_peer == NULL ||
        endpoint->endpoint_index >= SPW_IN_MEMORY_TRANSPORT_ENDPOINTS ||
        (message_capacity != 0u && message == NULL)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_message_size = 0u;
    *out_peer = (spw_transport_peer_id_t)SPW_TRANSPORT_PEER_ID_INITIALIZER;
    if (!endpoint->started) {
        return SPW_ERR_INVALID_STATE;
    }

    queue = &endpoint->link->inbox[endpoint->endpoint_index];
    if (queue->count == 0u) {
        return SPW_ERR_TIMEOUT;
    }

    slot = &queue->messages[queue->head];
    *out_message_size = slot->size;
    *out_peer = slot->source;
    if (message_capacity < slot->size) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }
    if (slot->size != 0u) {
        memcpy(message, slot->data, slot->size);
    }
    queue->head = (queue->head + 1u) % SPW_IN_MEMORY_TRANSPORT_QUEUE_DEPTH;
    --queue->count;
    return SPW_OK;
}

static spw_transport_ready_t memory_ready(
    const spw_in_memory_transport_endpoint_t* endpoint) {
    spw_transport_ready_t ready = SPW_TRANSPORT_READY_NONE;
    const spw_in_memory_transport_queue_t* inbound;
    const spw_in_memory_transport_queue_t* outbound;
    if (!endpoint->started || endpoint->link == NULL ||
        endpoint->endpoint_index >= SPW_IN_MEMORY_TRANSPORT_ENDPOINTS) {
        return ready;
    }
    inbound = &endpoint->link->inbox[endpoint->endpoint_index];
    outbound = &endpoint->link->inbox[remote_index(endpoint)];
    if (inbound->count != 0u) {
        ready |= SPW_TRANSPORT_READY_RX;
    }
    if (outbound->count < SPW_IN_MEMORY_TRANSPORT_QUEUE_DEPTH) {
        ready |= SPW_TRANSPORT_READY_TX;
    }
    return ready;
}

static spw_result_t memory_wait(void* context,
                                spw_transport_ready_t interests,
                                spw_timeout_us_t timeout_us,
                                spw_transport_ready_t* out_ready) {
    spw_in_memory_transport_endpoint_t* endpoint =
        (spw_in_memory_transport_endpoint_t*)context;
    spw_transport_ready_t ready;
    (void)timeout_us;

    if (endpoint == NULL || out_ready == NULL ||
        interests == SPW_TRANSPORT_READY_NONE ||
        (interests & (uint8_t)~SPW_TRANSPORT_READY_ALL) != 0u) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!endpoint->started) {
        *out_ready = SPW_TRANSPORT_READY_NONE;
        return SPW_ERR_INVALID_STATE;
    }
    ready = memory_ready(endpoint) & interests;
    *out_ready = ready;
    return ready == SPW_TRANSPORT_READY_NONE ? SPW_ERR_TIMEOUT : SPW_OK;
}

static spw_result_t memory_get_mtu(const void* context, size_t* out_mtu) {
    const spw_in_memory_transport_endpoint_t* endpoint =
        (const spw_in_memory_transport_endpoint_t*)context;
    if (endpoint == NULL || out_mtu == NULL || endpoint->link == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_mtu = SPW_IN_MEMORY_TRANSPORT_MAX_MESSAGE_SIZE;
    return SPW_OK;
}

static spw_result_t memory_get_state(const void* context,
                                     spw_transport_state_t* out_state) {
    const spw_in_memory_transport_endpoint_t* endpoint =
        (const spw_in_memory_transport_endpoint_t*)context;
    if (endpoint == NULL || out_state == NULL || endpoint->link == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_state = endpoint->started ? SPW_TRANSPORT_STATE_UP
                                   : SPW_TRANSPORT_STATE_DOWN;
    return SPW_OK;
}

static const spw_transport_provider_ops_t MEMORY_OPS = {
    memory_start,
    memory_stop,
    memory_reset,
    memory_send,
    memory_receive,
    memory_wait,
    memory_get_mtu,
    memory_get_state
};

void spw_in_memory_transport_link_init(spw_in_memory_transport_link_t* link) {
    if (link != NULL) {
        memset(link, 0, sizeof(*link));
    }
}

spw_result_t spw_in_memory_transport_endpoint_init(
    spw_in_memory_transport_endpoint_t* endpoint,
    spw_in_memory_transport_link_t* link,
    size_t endpoint_index,
    const spw_transport_peer_id_t* local_peer,
    const spw_transport_peer_id_t* remote_peer) {
    if (endpoint == NULL || link == NULL || local_peer == NULL ||
        remote_peer == NULL ||
        endpoint_index >= SPW_IN_MEMORY_TRANSPORT_ENDPOINTS ||
        local_peer->size == 0u || remote_peer->size == 0u ||
        local_peer->size > SPW_TRANSPORT_PEER_ID_MAX_SIZE ||
        remote_peer->size > SPW_TRANSPORT_PEER_ID_MAX_SIZE ||
        spw_transport_peer_id_equal(local_peer, remote_peer)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->link = link;
    endpoint->endpoint_index = endpoint_index;
    endpoint->local_peer = *local_peer;
    endpoint->remote_peer = *remote_peer;
    return SPW_OK;
}

void spw_in_memory_transport_provider(
    spw_in_memory_transport_endpoint_t* endpoint,
    spw_transport_provider_t* out_provider) {
    if (out_provider == NULL) {
        return;
    }
    out_provider->ops = endpoint == NULL ? NULL : &MEMORY_OPS;
    out_provider->context = endpoint;
}
