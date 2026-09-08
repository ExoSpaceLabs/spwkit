// SPDX-License-Identifier: Apache-2.0

#include "reference_driver.h"

#include <string.h>

static bool endpoint_running(const spw_reference_endpoint_t* endpoint) {
    return endpoint != NULL && endpoint->state == SPW_LINK_RUN;
}

static void clear_runtime_state(spw_reference_endpoint_t* endpoint) {
    size_t i;

    endpoint->rx_packet_head = 0u;
    endpoint->rx_packet_count = 0u;
    endpoint->rx_time_code_head = 0u;
    endpoint->rx_time_code_count = 0u;

    for (i = 0u; i < SPW_REFERENCE_DMA_SLOTS; ++i) {
        endpoint->tx_dma[i].length = 0u;
        endpoint->tx_dma[i].terminator = SPW_TERMINATOR_EOP;
        endpoint->tx_dma[i].state = SPW_REFERENCE_DMA_FREE;
        endpoint->rx_dma[i].length = 0u;
        endpoint->rx_dma[i].terminator = SPW_TERMINATOR_EOP;
        endpoint->rx_dma[i].state = SPW_REFERENCE_DMA_FREE;
    }
}

void spw_reference_link_init(spw_reference_link_t* link) {
    size_t endpoint_index;
    size_t slot_index;

    if (link == NULL) {
        return;
    }

    memset(link, 0, sizeof(*link));
    for (endpoint_index = 0u; endpoint_index < 2u; ++endpoint_index) {
        spw_reference_endpoint_t* endpoint = &link->endpoints[endpoint_index];
        endpoint->state = SPW_LINK_ERROR_RESET;
        endpoint->endpoint_index = (unsigned)endpoint_index;
        endpoint->peer = &link->endpoints[1u - endpoint_index];

        for (slot_index = 0u; slot_index < SPW_REFERENCE_DMA_SLOTS; ++slot_index) {
            endpoint->tx_dma[slot_index].token =
                (spw_driver_buffer_token_t)(UINT64_C(1000) +
                    (uint64_t)endpoint_index * UINT64_C(100) +
                    (uint64_t)slot_index);
            endpoint->rx_dma[slot_index].token =
                (spw_driver_buffer_token_t)(UINT64_C(2000) +
                    (uint64_t)endpoint_index * UINT64_C(100) +
                    (uint64_t)slot_index);
        }
        clear_runtime_state(endpoint);
    }
}

spw_reference_endpoint_t* spw_reference_link_endpoint(
    spw_reference_link_t* link,
    unsigned endpoint_index) {
    if (link == NULL || endpoint_index > 1u) {
        return NULL;
    }
    return &link->endpoints[endpoint_index];
}

static spw_result_t reference_start(void* raw) {
    spw_reference_endpoint_t* endpoint = (spw_reference_endpoint_t*)raw;
    if (endpoint == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    endpoint->state = SPW_LINK_RUN;
    return SPW_OK;
}

static spw_result_t reference_stop(void* raw) {
    spw_reference_endpoint_t* endpoint = (spw_reference_endpoint_t*)raw;
    if (endpoint == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    endpoint->state = SPW_LINK_READY;
    return SPW_OK;
}

static spw_result_t reference_reset(void* raw) {
    spw_reference_endpoint_t* endpoint = (spw_reference_endpoint_t*)raw;
    if (endpoint == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    endpoint->state = SPW_LINK_ERROR_RESET;
    clear_runtime_state(endpoint);
    return SPW_OK;
}

static spw_result_t reference_get_link_state(
    const void* raw,
    spw_link_state_t* out_state) {
    const spw_reference_endpoint_t* endpoint =
        (const spw_reference_endpoint_t*)raw;
    if (endpoint == NULL || out_state == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_state = endpoint->state;
    return SPW_OK;
}

static spw_result_t reference_get_capabilities(
    const void* raw,
    spw_capabilities_t* out_capabilities) {
    (void)raw;
    if (out_capabilities == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    memset(out_capabilities, 0, sizeof(*out_capabilities));
    out_capabilities->bits = SPW_CAP_EEP |
                             SPW_CAP_TIME_CODE |
                             SPW_CAP_LINK_CONTROL |
                             SPW_CAP_STATISTICS |
                             SPW_CAP_ZERO_COPY |
                             SPW_CAP_READINESS;
    out_capabilities->max_packet_size = SPW_REFERENCE_PACKET_CAPACITY;
    out_capabilities->tx_queue_depth = SPW_REFERENCE_QUEUE_DEPTH;
    out_capabilities->rx_queue_depth = SPW_REFERENCE_QUEUE_DEPTH;
    out_capabilities->buffer_alignment = sizeof(uint64_t);
    return SPW_OK;
}

static spw_result_t enqueue_packet(spw_reference_endpoint_t* endpoint,
                                   const spw_packet_t* packet) {
    size_t index;
    spw_reference_packet_slot_t* slot;

    if (endpoint->rx_packet_count >= SPW_REFERENCE_QUEUE_DEPTH) {
        ++endpoint->statistics.dropped_packets;
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }
    if (packet->length > SPW_REFERENCE_PACKET_CAPACITY) {
        return SPW_ERR_INVALID_PACKET;
    }

    index = (endpoint->rx_packet_head + endpoint->rx_packet_count) %
            SPW_REFERENCE_QUEUE_DEPTH;
    slot = &endpoint->rx_packets[index];
    if (packet->length != 0u) {
        memcpy(slot->bytes, packet->data, packet->length);
    }
    slot->length = packet->length;
    slot->terminator = packet->terminator;
    ++endpoint->rx_packet_count;
    return SPW_OK;
}

static spw_result_t reference_send(void* raw,
                                   const spw_packet_t* packet,
                                   spw_timeout_us_t timeout_us) {
    spw_reference_endpoint_t* endpoint = (spw_reference_endpoint_t*)raw;
    spw_result_t result;
    (void)timeout_us;

    if (endpoint == NULL || packet == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!endpoint_running(endpoint) || !endpoint_running(endpoint->peer)) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    if (packet->length != 0u && packet->data == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    result = enqueue_packet(endpoint->peer, packet);
    if (result != SPW_OK) {
        return result;
    }

    ++endpoint->statistics.tx_packets;
    endpoint->statistics.tx_bytes += packet->length;
    if (packet->terminator == SPW_TERMINATOR_EEP) {
        ++endpoint->statistics.eep_packets;
    }
    return SPW_OK;
}

static spw_result_t reference_receive(void* raw,
                                      spw_packet_t* packet,
                                      spw_timeout_us_t timeout_us) {
    spw_reference_endpoint_t* endpoint = (spw_reference_endpoint_t*)raw;
    spw_reference_packet_slot_t* slot;
    (void)timeout_us;

    if (endpoint == NULL || packet == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!endpoint_running(endpoint)) {
        return SPW_ERR_INVALID_STATE;
    }
    if (endpoint->rx_packet_count == 0u) {
        return SPW_ERR_TIMEOUT;
    }

    slot = &endpoint->rx_packets[endpoint->rx_packet_head];
    packet->length = slot->length;
    packet->terminator = slot->terminator;
    if (packet->capacity < slot->length) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }
    if (slot->length != 0u && packet->data == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    if (slot->length != 0u) {
        memcpy(packet->data, slot->bytes, slot->length);
    }
    endpoint->rx_packet_head =
        (endpoint->rx_packet_head + 1u) % SPW_REFERENCE_QUEUE_DEPTH;
    --endpoint->rx_packet_count;
    ++endpoint->statistics.rx_packets;
    endpoint->statistics.rx_bytes += slot->length;
    return SPW_OK;
}

static spw_result_t reference_send_time_code(
    void* raw,
    const spw_time_code_t* time_code,
    spw_timeout_us_t timeout_us) {
    spw_reference_endpoint_t* endpoint = (spw_reference_endpoint_t*)raw;
    spw_reference_endpoint_t* peer;
    size_t index;
    (void)timeout_us;

    if (endpoint == NULL || time_code == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    peer = endpoint->peer;
    if (!endpoint_running(endpoint) || !endpoint_running(peer)) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    if (peer->rx_time_code_count >= SPW_REFERENCE_QUEUE_DEPTH) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    index = (peer->rx_time_code_head + peer->rx_time_code_count) %
            SPW_REFERENCE_QUEUE_DEPTH;
    peer->rx_time_codes[index] = *time_code;
    ++peer->rx_time_code_count;
    ++endpoint->statistics.tx_time_codes;
    return SPW_OK;
}

static spw_result_t reference_receive_time_code(
    void* raw,
    spw_time_code_t* time_code,
    spw_timeout_us_t timeout_us) {
    spw_reference_endpoint_t* endpoint = (spw_reference_endpoint_t*)raw;
    (void)timeout_us;

    if (endpoint == NULL || time_code == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!endpoint_running(endpoint)) {
        return SPW_ERR_INVALID_STATE;
    }
    if (endpoint->rx_time_code_count == 0u) {
        return SPW_ERR_TIMEOUT;
    }

    *time_code = endpoint->rx_time_codes[endpoint->rx_time_code_head];
    endpoint->rx_time_code_head =
        (endpoint->rx_time_code_head + 1u) % SPW_REFERENCE_QUEUE_DEPTH;
    --endpoint->rx_time_code_count;
    ++endpoint->statistics.rx_time_codes;
    return SPW_OK;
}

static spw_result_t reference_get_statistics(
    const void* raw,
    spw_statistics_t* out_statistics) {
    const spw_reference_endpoint_t* endpoint =
        (const spw_reference_endpoint_t*)raw;
    if (endpoint == NULL || out_statistics == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_statistics = endpoint->statistics;
    return SPW_OK;
}

static spw_result_t reference_clear_statistics(void* raw) {
    spw_reference_endpoint_t* endpoint = (spw_reference_endpoint_t*)raw;
    if (endpoint == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    memset(&endpoint->statistics, 0, sizeof(endpoint->statistics));
    return SPW_OK;
}

static spw_result_t reference_wait(void* raw,
                                   spw_ready_events_t interests,
                                   spw_timeout_us_t timeout_us,
                                   spw_ready_events_t* out_ready) {
    spw_reference_endpoint_t* endpoint = (spw_reference_endpoint_t*)raw;
    spw_ready_events_t ready = SPW_READY_NONE;
    (void)timeout_us;

    if (endpoint == NULL || out_ready == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!endpoint_running(endpoint)) {
        return SPW_ERR_INVALID_STATE;
    }
    if (endpoint->rx_packet_count != 0u) {
        ready |= SPW_READY_RX_PACKET;
    }
    if (endpoint->rx_time_code_count != 0u) {
        ready |= SPW_READY_RX_TIME_CODE;
    }

    *out_ready = ready & interests;
    return *out_ready != SPW_READY_NONE ? SPW_OK : SPW_ERR_TIMEOUT;
}

static spw_driver_buffer_t dma_descriptor(spw_reference_dma_slot_t* slot) {
    spw_driver_buffer_t buffer;
    buffer.data = slot->bytes;
    buffer.length = slot->length;
    buffer.capacity = sizeof(slot->bytes);
    buffer.terminator = slot->terminator;
    buffer.token = slot->token;
    return buffer;
}

static spw_reference_dma_slot_t* find_dma_token(
    spw_reference_dma_slot_t slots[SPW_REFERENCE_DMA_SLOTS],
    spw_driver_buffer_token_t token) {
    size_t i;
    for (i = 0u; i < SPW_REFERENCE_DMA_SLOTS; ++i) {
        if (slots[i].token == token) {
            return &slots[i];
        }
    }
    return NULL;
}

static spw_result_t reference_acquire_tx_buffer(
    void* raw,
    size_t min_capacity,
    spw_timeout_us_t timeout_us,
    spw_driver_buffer_t* out_buffer) {
    spw_reference_endpoint_t* endpoint = (spw_reference_endpoint_t*)raw;
    size_t i;
    (void)timeout_us;

    if (endpoint == NULL || out_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!endpoint_running(endpoint)) {
        return SPW_ERR_INVALID_STATE;
    }
    if (min_capacity > SPW_REFERENCE_PACKET_CAPACITY) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    for (i = 0u; i < SPW_REFERENCE_DMA_SLOTS; ++i) {
        if (endpoint->tx_dma[i].state == SPW_REFERENCE_DMA_FREE) {
            endpoint->tx_dma[i].state = SPW_REFERENCE_DMA_TX_APP;
            endpoint->tx_dma[i].length = 0u;
            endpoint->tx_dma[i].terminator = SPW_TERMINATOR_EOP;
            *out_buffer = dma_descriptor(&endpoint->tx_dma[i]);
            return SPW_OK;
        }
    }
    return SPW_ERR_RESOURCE_EXHAUSTED;
}

static spw_reference_dma_slot_t* find_free_peer_rx(
    spw_reference_endpoint_t* peer) {
    size_t i;
    for (i = 0u; i < SPW_REFERENCE_DMA_SLOTS; ++i) {
        if (peer->rx_dma[i].state == SPW_REFERENCE_DMA_FREE) {
            return &peer->rx_dma[i];
        }
    }
    return NULL;
}

static spw_result_t reference_submit_tx_buffer(
    void* raw,
    const spw_driver_buffer_t* buffer,
    spw_timeout_us_t timeout_us) {
    spw_reference_endpoint_t* endpoint = (spw_reference_endpoint_t*)raw;
    spw_reference_dma_slot_t* tx_slot;
    spw_reference_dma_slot_t* rx_slot;
    (void)timeout_us;

    if (endpoint == NULL || buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!endpoint_running(endpoint) || !endpoint_running(endpoint->peer)) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    tx_slot = find_dma_token(endpoint->tx_dma, buffer->token);
    if (tx_slot == NULL || tx_slot->state != SPW_REFERENCE_DMA_TX_APP ||
        buffer->data != tx_slot->bytes ||
        buffer->capacity != SPW_REFERENCE_PACKET_CAPACITY ||
        buffer->length > buffer->capacity) {
        return SPW_ERR_BACKEND;
    }

    rx_slot = find_free_peer_rx(endpoint->peer);
    if (rx_slot == NULL) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    if (buffer->length != 0u) {
        memcpy(rx_slot->bytes, buffer->data, buffer->length);
    }
    rx_slot->length = buffer->length;
    rx_slot->terminator = buffer->terminator;
    rx_slot->state = SPW_REFERENCE_DMA_RX_READY;

    tx_slot->length = buffer->length;
    tx_slot->terminator = buffer->terminator;
    tx_slot->state = SPW_REFERENCE_DMA_TX_COMPLETE;

    ++endpoint->statistics.tx_packets;
    endpoint->statistics.tx_bytes += buffer->length;
    if (buffer->terminator == SPW_TERMINATOR_EEP) {
        ++endpoint->statistics.eep_packets;
    }
    return SPW_OK;
}

static spw_result_t reference_reclaim_tx_buffer(
    void* raw,
    spw_timeout_us_t timeout_us,
    spw_driver_buffer_t* out_buffer) {
    spw_reference_endpoint_t* endpoint = (spw_reference_endpoint_t*)raw;
    size_t i;
    (void)timeout_us;

    if (endpoint == NULL || out_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    for (i = 0u; i < SPW_REFERENCE_DMA_SLOTS; ++i) {
        if (endpoint->tx_dma[i].state == SPW_REFERENCE_DMA_TX_COMPLETE) {
            endpoint->tx_dma[i].state = SPW_REFERENCE_DMA_TX_APP;
            *out_buffer = dma_descriptor(&endpoint->tx_dma[i]);
            return SPW_OK;
        }
    }
    return SPW_ERR_TIMEOUT;
}

static spw_result_t reference_release_tx_buffer(
    void* raw,
    const spw_driver_buffer_t* buffer) {
    spw_reference_endpoint_t* endpoint = (spw_reference_endpoint_t*)raw;
    spw_reference_dma_slot_t* slot;

    if (endpoint == NULL || buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    slot = find_dma_token(endpoint->tx_dma, buffer->token);
    if (slot == NULL || slot->state != SPW_REFERENCE_DMA_TX_APP ||
        buffer->data != slot->bytes) {
        return SPW_ERR_BACKEND;
    }
    slot->state = SPW_REFERENCE_DMA_FREE;
    slot->length = 0u;
    return SPW_OK;
}

static spw_result_t reference_acquire_rx_buffer(
    void* raw,
    spw_timeout_us_t timeout_us,
    spw_driver_buffer_t* out_buffer) {
    spw_reference_endpoint_t* endpoint = (spw_reference_endpoint_t*)raw;
    size_t i;
    (void)timeout_us;

    if (endpoint == NULL || out_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!endpoint_running(endpoint)) {
        return SPW_ERR_INVALID_STATE;
    }
    for (i = 0u; i < SPW_REFERENCE_DMA_SLOTS; ++i) {
        if (endpoint->rx_dma[i].state == SPW_REFERENCE_DMA_RX_READY) {
            endpoint->rx_dma[i].state = SPW_REFERENCE_DMA_RX_APP;
            *out_buffer = dma_descriptor(&endpoint->rx_dma[i]);
            ++endpoint->statistics.rx_packets;
            endpoint->statistics.rx_bytes += endpoint->rx_dma[i].length;
            return SPW_OK;
        }
    }
    return SPW_ERR_TIMEOUT;
}

static spw_result_t reference_release_rx_buffer(
    void* raw,
    const spw_driver_buffer_t* buffer) {
    spw_reference_endpoint_t* endpoint = (spw_reference_endpoint_t*)raw;
    spw_reference_dma_slot_t* slot;

    if (endpoint == NULL || buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    slot = find_dma_token(endpoint->rx_dma, buffer->token);
    if (slot == NULL || slot->state != SPW_REFERENCE_DMA_RX_APP ||
        buffer->data != slot->bytes) {
        return SPW_ERR_BACKEND;
    }
    slot->state = SPW_REFERENCE_DMA_FREE;
    slot->length = 0u;
    return SPW_OK;
}

static spw_result_t reference_sync_buffer(
    void* raw,
    const spw_driver_buffer_t* buffer,
    spw_driver_sync_direction_t direction) {
    spw_reference_endpoint_t* endpoint = (spw_reference_endpoint_t*)raw;
    (void)buffer;

    if (endpoint == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (direction == SPW_DRIVER_SYNC_TO_DEVICE) {
        ++endpoint->sync_to_device_count;
        return SPW_OK;
    }
    if (direction == SPW_DRIVER_SYNC_FROM_DEVICE) {
        ++endpoint->sync_from_device_count;
        return SPW_OK;
    }
    return SPW_ERR_INVALID_ARGUMENT;
}

static const spw_driver_ops_t REFERENCE_OPS = {
    sizeof(spw_driver_ops_t), SPW_DRIVER_OPS_VERSION,
    reference_start, reference_stop, reference_reset,
    reference_get_link_state, reference_get_capabilities,
    reference_send, reference_receive,
    reference_send_time_code, reference_receive_time_code,
    reference_get_statistics, reference_clear_statistics,
    reference_wait,
    reference_acquire_tx_buffer, reference_submit_tx_buffer,
    reference_reclaim_tx_buffer, reference_release_tx_buffer,
    reference_acquire_rx_buffer, reference_release_rx_buffer,
    reference_sync_buffer
};

const spw_driver_ops_t* spw_reference_driver_ops(void) {
    return &REFERENCE_OPS;
}

bool spw_reference_owns_tx_pointer(const spw_reference_endpoint_t* endpoint,
                                   const uint8_t* pointer) {
    size_t i;
    if (endpoint == NULL || pointer == NULL) {
        return false;
    }
    for (i = 0u; i < SPW_REFERENCE_DMA_SLOTS; ++i) {
        if (pointer == endpoint->tx_dma[i].bytes) {
            return true;
        }
    }
    return false;
}

bool spw_reference_owns_rx_pointer(const spw_reference_endpoint_t* endpoint,
                                   const uint8_t* pointer) {
    size_t i;
    if (endpoint == NULL || pointer == NULL) {
        return false;
    }
    for (i = 0u; i < SPW_REFERENCE_DMA_SLOTS; ++i) {
        if (pointer == endpoint->rx_dma[i].bytes) {
            return true;
        }
    }
    return false;
}
