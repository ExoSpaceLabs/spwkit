// SPDX-License-Identifier: Apache-2.0

#include "backends/virtual/simulator_backend.h"
#include "core/buffer_internal.h"
#include "platform/host_sync.h"

#include <spwkit/simulator.h>

#include <limits.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    SPW_SIMULATOR_MAX_PACKET_SIZE = 4096,
    SPW_SIMULATOR_PACKET_QUEUE_DEPTH = 8,
    SPW_SIMULATOR_TIME_CODE_QUEUE_DEPTH = 8,
    SPW_SIMULATOR_MAX_LOCAL_LINKS = 16
};

typedef union spw_simulator_max_alignment {
    long double long_double_value;
    void* pointer_value;
    uint64_t integer_value;
} spw_simulator_max_alignment_t;

typedef struct spw_simulator_packet_slot {
    uint8_t data[SPW_SIMULATOR_MAX_PACKET_SIZE];
    size_t length;
    spw_terminator_t terminator;
} spw_simulator_packet_slot_t;

typedef struct spw_simulator_packet_queue {
    spw_simulator_packet_slot_t slots[SPW_SIMULATOR_PACKET_QUEUE_DEPTH];
    size_t head;
    size_t tail;
    size_t count;
} spw_simulator_packet_queue_t;

typedef struct spw_simulator_time_code_queue {
    spw_time_code_t slots[SPW_SIMULATOR_TIME_CODE_QUEUE_DEPTH];
    size_t head;
    size_t tail;
    size_t count;
} spw_simulator_time_code_queue_t;

typedef struct spw_simulator_endpoint_state {
    bool attached;
    bool started;
    spw_link_state_t state;
    spw_simulator_packet_queue_t packets;
    spw_simulator_time_code_queue_t time_codes;
    spw_statistics_t statistics;
} spw_simulator_endpoint_state_t;

typedef struct spw_simulator_virtual_link {
    spw_host_mutex_t mutex;
    spw_host_condition_t condition;
    bool sync_initialized;
    bool allocated;
    uint64_t link_id;
    spw_simulator_endpoint_state_t endpoints[2];
} spw_simulator_virtual_link_t;

typedef struct spw_simulator_tx_buffer_slot {
    union {
        spw_simulator_max_alignment_t alignment;
        uint8_t bytes[SPW_SIMULATOR_MAX_PACKET_SIZE];
    } storage;
    struct spw_buffer descriptor;
} spw_simulator_tx_buffer_slot_t;

typedef struct spw_simulator_backend {
    uint64_t link_id;
    size_t endpoint_index;
    spw_simulator_virtual_link_t* link;

    spw_simulator_tx_buffer_slot_t tx_buffers[SPW_SIMULATOR_PACKET_QUEUE_DEPTH];
    union {
        spw_simulator_max_alignment_t alignment;
        uint8_t bytes[SPW_SIMULATOR_MAX_PACKET_SIZE];
    } rx_storage;
    struct spw_buffer rx_buffer;
    bool rx_buffer_acquired;
    spw_host_mutex_t zero_copy_mutex;
    spw_host_condition_t zero_copy_condition;
    bool zero_copy_sync_initialized;
} spw_simulator_backend_t;

static spw_host_mutex_t REGISTRY_MUTEX = SPW_HOST_MUTEX_STATIC_INITIALIZER;
static spw_simulator_virtual_link_t REGISTRY[SPW_SIMULATOR_MAX_LOCAL_LINKS];

static bool valid_terminator(spw_terminator_t terminator) {
    return terminator == SPW_TERMINATOR_EOP || terminator == SPW_TERMINATOR_EEP;
}

static bool valid_time_code(const spw_time_code_t* time_code) {
    return time_code != NULL && time_code->time_count <= 63u &&
           time_code->control_flags == 0u;
}

static void reset_endpoint(spw_simulator_endpoint_state_t* endpoint) {
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->state = SPW_LINK_ERROR_RESET;
}

static void clear_receive_queues(spw_simulator_endpoint_state_t* endpoint) {
    endpoint->statistics.dropped_packets += endpoint->packets.count;
    memset(&endpoint->packets, 0, sizeof(endpoint->packets));
    memset(&endpoint->time_codes, 0, sizeof(endpoint->time_codes));
}

static void refresh_link_states(spw_simulator_endpoint_state_t* a,
                                spw_simulator_endpoint_state_t* b) {
    if (a->started && a->attached && b->started && b->attached) {
        a->state = SPW_LINK_RUN;
        b->state = SPW_LINK_RUN;
        return;
    }
    if (a->started && a->attached) {
        a->state = SPW_LINK_CONNECTING;
    }
    if (b->started && b->attached) {
        b->state = SPW_LINK_CONNECTING;
    }
}

static bool peer_available(const spw_simulator_endpoint_state_t* peer) {
    return peer->attached && peer->started;
}

static uint64_t deadline_from_timeout(spw_timeout_us_t timeout_us) {
    const uint64_t now = spw_host_now_us();
    if (UINT64_MAX - now < timeout_us) {
        return UINT64_MAX;
    }
    return now + timeout_us;
}

static bool wait_condition(spw_host_condition_t* condition,
                           spw_host_mutex_t* mutex,
                           spw_timeout_us_t timeout_us,
                           bool (*predicate)(void*),
                           void* predicate_context) {
    uint64_t deadline = 0u;
    if (predicate(predicate_context)) {
        return true;
    }
    if (timeout_us == SPW_TIMEOUT_IMMEDIATE) {
        return false;
    }
    if (timeout_us != SPW_TIMEOUT_INFINITE) {
        deadline = deadline_from_timeout(timeout_us);
    }

    while (!predicate(predicate_context)) {
        if (timeout_us == SPW_TIMEOUT_INFINITE) {
            if (!spw_host_condition_wait(condition, mutex, 0u, true)) {
                return false;
            }
        } else {
            const uint64_t now = spw_host_now_us();
            uint64_t remaining;
            if (now >= deadline) {
                return false;
            }
            remaining = deadline - now;
            if (!spw_host_condition_wait(condition, mutex, remaining, false)) {
                return predicate(predicate_context);
            }
        }
    }
    return true;
}

static bool initialize_link_sync(spw_simulator_virtual_link_t* link) {
    if (link->sync_initialized) {
        return true;
    }
    if (!spw_host_mutex_init(&link->mutex)) {
        return false;
    }
    if (!spw_host_condition_init(&link->condition)) {
        spw_host_mutex_destroy(&link->mutex);
        return false;
    }
    link->sync_initialized = true;
    return true;
}

static spw_result_t attach_backend(spw_simulator_backend_t* backend) {
    spw_simulator_virtual_link_t* target = NULL;
    spw_simulator_virtual_link_t* free_slot = NULL;
    size_t i;

    if (backend->link != NULL) {
        return SPW_ERR_INVALID_STATE;
    }

    spw_host_mutex_lock(&REGISTRY_MUTEX);
    for (i = 0u; i < SPW_SIMULATOR_MAX_LOCAL_LINKS; ++i) {
        spw_simulator_virtual_link_t* candidate = &REGISTRY[i];
        if (candidate->allocated && candidate->link_id == backend->link_id) {
            target = candidate;
            break;
        }
        if (!candidate->allocated && free_slot == NULL) {
            free_slot = candidate;
        }
    }

    if (target == NULL) {
        target = free_slot;
        if (target == NULL) {
            spw_host_mutex_unlock(&REGISTRY_MUTEX);
            return SPW_ERR_RESOURCE_EXHAUSTED;
        }
    }

    if (!initialize_link_sync(target)) {
        spw_host_mutex_unlock(&REGISTRY_MUTEX);
        return SPW_ERR_BACKEND;
    }

    spw_host_mutex_lock(&target->mutex);
    if (!target->allocated) {
        target->allocated = true;
        target->link_id = backend->link_id;
        reset_endpoint(&target->endpoints[0]);
        reset_endpoint(&target->endpoints[1]);
    }

    if (target->endpoints[backend->endpoint_index].attached) {
        spw_host_mutex_unlock(&target->mutex);
        spw_host_mutex_unlock(&REGISTRY_MUTEX);
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    reset_endpoint(&target->endpoints[backend->endpoint_index]);
    target->endpoints[backend->endpoint_index].attached = true;
    backend->link = target;
    refresh_link_states(&target->endpoints[0], &target->endpoints[1]);
    spw_host_condition_broadcast(&target->condition);
    spw_host_mutex_unlock(&target->mutex);
    spw_host_mutex_unlock(&REGISTRY_MUTEX);
    return SPW_OK;
}

static void detach_backend(spw_simulator_backend_t* backend) {
    spw_simulator_endpoint_state_t* local;
    spw_simulator_endpoint_state_t* peer;
    spw_simulator_virtual_link_t* link;

    if (backend->link == NULL) {
        return;
    }

    link = backend->link;
    spw_host_mutex_lock(&REGISTRY_MUTEX);
    spw_host_mutex_lock(&link->mutex);

    local = &link->endpoints[backend->endpoint_index];
    peer = &link->endpoints[1u - backend->endpoint_index];
    reset_endpoint(local);

    if (peer->attached && peer->started) {
        peer->state = SPW_LINK_CONNECTING;
        ++peer->statistics.link_errors;
    }

    if (!link->endpoints[0].attached && !link->endpoints[1].attached) {
        link->allocated = false;
        link->link_id = 0u;
        reset_endpoint(&link->endpoints[0]);
        reset_endpoint(&link->endpoints[1]);
    }

    spw_host_condition_broadcast(&link->condition);
    backend->link = NULL;
    spw_host_mutex_unlock(&link->mutex);
    spw_host_mutex_unlock(&REGISTRY_MUTEX);
}

static void initialize_zero_copy_buffers(spw_simulator_backend_t* backend) {
    size_t i;
    for (i = 0u; i < SPW_SIMULATOR_PACKET_QUEUE_DEPTH; ++i) {
        struct spw_buffer* descriptor = &backend->tx_buffers[i].descriptor;
        descriptor->data = backend->tx_buffers[i].storage.bytes;
        descriptor->length = 0u;
        descriptor->capacity = SPW_SIMULATOR_MAX_PACKET_SIZE;
        descriptor->terminator = SPW_TERMINATOR_EOP;
        descriptor->owner = backend;
        descriptor->direction = SPW_BUFFER_DIRECTION_TX;
        descriptor->state = SPW_BUFFER_STATE_FREE;
        descriptor->token = i;
    }

    backend->rx_buffer.data = backend->rx_storage.bytes;
    backend->rx_buffer.length = 0u;
    backend->rx_buffer.capacity = SPW_SIMULATOR_MAX_PACKET_SIZE;
    backend->rx_buffer.terminator = SPW_TERMINATOR_EOP;
    backend->rx_buffer.owner = backend;
    backend->rx_buffer.direction = SPW_BUFFER_DIRECTION_RX;
    backend->rx_buffer.state = SPW_BUFFER_STATE_FREE;
    backend->rx_buffer.token = 0u;
}

static spw_result_t simulator_construct(void* context,
                                        const spw_port_config_t* config) {
    spw_simulator_backend_t* backend = (spw_simulator_backend_t*)context;
    const spw_simulator_config_t* simulator =
        (const spw_simulator_config_t*)config->backend_config;
    spw_result_t result;

    memset(backend, 0, sizeof(*backend));
    backend->link_id = simulator->link_id;
    backend->endpoint_index =
        simulator->endpoint == SPW_SIMULATOR_ENDPOINT_B ? 1u : 0u;

    if (!spw_host_mutex_init(&backend->zero_copy_mutex)) {
        return SPW_ERR_BACKEND;
    }
    if (!spw_host_condition_init(&backend->zero_copy_condition)) {
        spw_host_mutex_destroy(&backend->zero_copy_mutex);
        return SPW_ERR_BACKEND;
    }
    backend->zero_copy_sync_initialized = true;
    initialize_zero_copy_buffers(backend);

    result = attach_backend(backend);
    if (result != SPW_OK) {
        spw_host_condition_destroy(&backend->zero_copy_condition);
        spw_host_mutex_destroy(&backend->zero_copy_mutex);
        backend->zero_copy_sync_initialized = false;
    }
    return result;
}

static void simulator_destroy(void* context) {
    spw_simulator_backend_t* backend = (spw_simulator_backend_t*)context;
    detach_backend(backend);
    if (backend->zero_copy_sync_initialized) {
        spw_host_condition_destroy(&backend->zero_copy_condition);
        spw_host_mutex_destroy(&backend->zero_copy_mutex);
        backend->zero_copy_sync_initialized = false;
    }
}

static spw_result_t simulator_start(void* context) {
    spw_simulator_backend_t* backend = (spw_simulator_backend_t*)context;
    spw_simulator_endpoint_state_t* local;
    if (backend->link == NULL) {
        return SPW_ERR_INVALID_STATE;
    }

    spw_host_mutex_lock(&backend->link->mutex);
    local = &backend->link->endpoints[backend->endpoint_index];
    if (!local->attached) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    local->started = true;
    local->state = SPW_LINK_CONNECTING;
    refresh_link_states(&backend->link->endpoints[0], &backend->link->endpoints[1]);
    spw_host_condition_broadcast(&backend->link->condition);
    spw_host_mutex_unlock(&backend->link->mutex);
    return SPW_OK;
}

static spw_result_t simulator_stop(void* context) {
    spw_simulator_backend_t* backend = (spw_simulator_backend_t*)context;
    spw_simulator_endpoint_state_t* local;
    spw_simulator_endpoint_state_t* peer;
    if (backend->link == NULL) {
        return SPW_ERR_INVALID_STATE;
    }

    spw_host_mutex_lock(&backend->link->mutex);
    local = &backend->link->endpoints[backend->endpoint_index];
    peer = &backend->link->endpoints[1u - backend->endpoint_index];
    if (!local->attached) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    local->started = false;
    local->state = SPW_LINK_READY;
    if (peer->attached && peer->started) {
        peer->state = SPW_LINK_CONNECTING;
    }
    spw_host_condition_broadcast(&backend->link->condition);
    spw_host_mutex_unlock(&backend->link->mutex);
    return SPW_OK;
}

static spw_result_t simulator_reset(void* context) {
    spw_simulator_backend_t* backend = (spw_simulator_backend_t*)context;
    spw_simulator_endpoint_state_t* local;
    spw_simulator_endpoint_state_t* peer;
    if (backend->link == NULL) {
        return SPW_ERR_INVALID_STATE;
    }

    spw_host_mutex_lock(&backend->link->mutex);
    local = &backend->link->endpoints[backend->endpoint_index];
    peer = &backend->link->endpoints[1u - backend->endpoint_index];
    if (!local->attached) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    local->started = false;
    local->state = SPW_LINK_ERROR_RESET;
    clear_receive_queues(local);
    if (peer->attached && peer->started) {
        peer->state = SPW_LINK_CONNECTING;
        ++peer->statistics.link_errors;
    }
    spw_host_condition_broadcast(&backend->link->condition);
    spw_host_mutex_unlock(&backend->link->mutex);
    return SPW_OK;
}

static spw_result_t simulator_get_link_state(const void* context,
                                              spw_link_state_t* out_state) {
    const spw_simulator_backend_t* backend =
        (const spw_simulator_backend_t*)context;
    const spw_simulator_endpoint_state_t* local;
    if (backend->link == NULL) {
        return SPW_ERR_INVALID_STATE;
    }

    spw_host_mutex_lock(&backend->link->mutex);
    local = &backend->link->endpoints[backend->endpoint_index];
    if (!local->attached) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    *out_state = local->state;
    spw_host_mutex_unlock(&backend->link->mutex);
    return SPW_OK;
}

static spw_result_t simulator_get_capabilities(
    const void* context,
    spw_capabilities_t* out_capabilities) {
    (void)context;
    out_capabilities->bits = SPW_CAP_EEP | SPW_CAP_TIME_CODE |
                             SPW_CAP_LINK_CONTROL | SPW_CAP_STATISTICS;
    out_capabilities->max_packet_size = SPW_SIMULATOR_MAX_PACKET_SIZE;
    out_capabilities->tx_queue_depth = SPW_SIMULATOR_PACKET_QUEUE_DEPTH;
    out_capabilities->rx_queue_depth = SPW_SIMULATOR_PACKET_QUEUE_DEPTH;
    out_capabilities->buffer_alignment = alignof(spw_simulator_max_alignment_t);
    return SPW_OK;
}

static bool simulator_supports_zero_copy(const void* context) {
    (void)context;
    return true;
}

typedef struct spw_packet_space_predicate {
    spw_simulator_endpoint_state_t* local;
    spw_simulator_endpoint_state_t* peer;
} spw_packet_space_predicate_t;

static bool packet_space_ready(void* context) {
    spw_packet_space_predicate_t* state = (spw_packet_space_predicate_t*)context;
    return !state->local->started || !peer_available(state->peer) ||
           state->peer->packets.count < SPW_SIMULATOR_PACKET_QUEUE_DEPTH;
}

static bool time_code_space_ready(void* context) {
    spw_packet_space_predicate_t* state = (spw_packet_space_predicate_t*)context;
    return !state->local->started || !peer_available(state->peer) ||
           state->peer->time_codes.count < SPW_SIMULATOR_TIME_CODE_QUEUE_DEPTH;
}

static bool packet_receive_ready(void* context) {
    spw_packet_space_predicate_t* state = (spw_packet_space_predicate_t*)context;
    return state->local->packets.count > 0u || !state->local->started ||
           !peer_available(state->peer);
}

static bool time_code_receive_ready(void* context) {
    spw_packet_space_predicate_t* state = (spw_packet_space_predicate_t*)context;
    return state->local->time_codes.count > 0u || !state->local->started ||
           !peer_available(state->peer);
}

static spw_result_t simulator_send(void* context,
                                   const spw_packet_t* packet,
                                   spw_timeout_us_t timeout_us) {
    spw_simulator_backend_t* backend = (spw_simulator_backend_t*)context;
    spw_simulator_endpoint_state_t* local;
    spw_simulator_endpoint_state_t* peer;
    spw_simulator_packet_slot_t* slot;
    spw_packet_space_predicate_t predicate;
    bool ready;

    if (backend->link == NULL) {
        return SPW_ERR_INVALID_STATE;
    }
    if (!valid_terminator(packet->terminator) ||
        packet->length > SPW_SIMULATOR_MAX_PACKET_SIZE) {
        return SPW_ERR_INVALID_PACKET;
    }
    if (packet->length > 0u && packet->data == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (packet->capacity != 0u && packet->capacity < packet->length) {
        return SPW_ERR_INVALID_PACKET;
    }

    spw_host_mutex_lock(&backend->link->mutex);
    local = &backend->link->endpoints[backend->endpoint_index];
    peer = &backend->link->endpoints[1u - backend->endpoint_index];
    if (!local->attached || !local->started) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_INVALID_STATE;
    }
    if (!peer_available(peer)) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_LINK_UNAVAILABLE;
    }

    predicate.local = local;
    predicate.peer = peer;
    ready = wait_condition(&backend->link->condition, &backend->link->mutex,
                           timeout_us, packet_space_ready, &predicate);
    if (!ready) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return timeout_us == SPW_TIMEOUT_IMMEDIATE
                   ? SPW_ERR_RESOURCE_EXHAUSTED
                   : SPW_ERR_TIMEOUT;
    }
    if (!local->started) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_INVALID_STATE;
    }
    if (!peer_available(peer)) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    if (peer->packets.count == SPW_SIMULATOR_PACKET_QUEUE_DEPTH) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_TIMEOUT;
    }

    slot = &peer->packets.slots[peer->packets.tail];
    if (packet->length > 0u) {
        memcpy(slot->data, packet->data, packet->length);
    }
    slot->length = packet->length;
    slot->terminator = packet->terminator;
    peer->packets.tail =
        (peer->packets.tail + 1u) % SPW_SIMULATOR_PACKET_QUEUE_DEPTH;
    ++peer->packets.count;

    ++local->statistics.tx_packets;
    local->statistics.tx_bytes += packet->length;
    if (packet->terminator == SPW_TERMINATOR_EEP) {
        ++local->statistics.eep_packets;
    }
    spw_host_condition_broadcast(&backend->link->condition);
    spw_host_mutex_unlock(&backend->link->mutex);
    return SPW_OK;
}

static spw_result_t simulator_receive(void* context,
                                      spw_packet_t* packet,
                                      spw_timeout_us_t timeout_us) {
    spw_simulator_backend_t* backend = (spw_simulator_backend_t*)context;
    spw_simulator_endpoint_state_t* local;
    spw_simulator_endpoint_state_t* peer;
    const spw_simulator_packet_slot_t* slot;
    spw_packet_space_predicate_t predicate;
    bool ready;

    if (backend->link == NULL) {
        return SPW_ERR_INVALID_STATE;
    }

    spw_host_mutex_lock(&backend->link->mutex);
    local = &backend->link->endpoints[backend->endpoint_index];
    peer = &backend->link->endpoints[1u - backend->endpoint_index];
    if (!local->attached || !local->started) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_INVALID_STATE;
    }

    predicate.local = local;
    predicate.peer = peer;
    ready = wait_condition(&backend->link->condition, &backend->link->mutex,
                           timeout_us, packet_receive_ready, &predicate);
    if (!ready) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_TIMEOUT;
    }
    if (!local->started) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_INVALID_STATE;
    }
    if (local->packets.count == 0u) {
        spw_result_t result = !peer_available(peer)
                                  ? SPW_ERR_LINK_UNAVAILABLE
                                  : SPW_ERR_TIMEOUT;
        spw_host_mutex_unlock(&backend->link->mutex);
        return result;
    }

    slot = &local->packets.slots[local->packets.head];
    packet->length = slot->length;
    packet->terminator = slot->terminator;
    if (slot->length > packet->capacity) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_BUFFER_TOO_SMALL;
    }
    if (slot->length > 0u && packet->data == NULL) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (slot->length > 0u) {
        memcpy(packet->data, slot->data, slot->length);
    }
    local->packets.head =
        (local->packets.head + 1u) % SPW_SIMULATOR_PACKET_QUEUE_DEPTH;
    --local->packets.count;
    ++local->statistics.rx_packets;
    local->statistics.rx_bytes += slot->length;
    spw_host_condition_broadcast(&backend->link->condition);
    spw_host_mutex_unlock(&backend->link->mutex);
    return SPW_OK;
}

static spw_result_t simulator_send_time_code(void* context,
                                             const spw_time_code_t* time_code,
                                             spw_timeout_us_t timeout_us) {
    spw_simulator_backend_t* backend = (spw_simulator_backend_t*)context;
    spw_simulator_endpoint_state_t* local;
    spw_simulator_endpoint_state_t* peer;
    spw_packet_space_predicate_t predicate;
    bool ready;

    if (backend->link == NULL) {
        return SPW_ERR_INVALID_STATE;
    }
    if (!valid_time_code(time_code)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    spw_host_mutex_lock(&backend->link->mutex);
    local = &backend->link->endpoints[backend->endpoint_index];
    peer = &backend->link->endpoints[1u - backend->endpoint_index];
    if (!local->attached || !local->started) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_INVALID_STATE;
    }
    if (!peer_available(peer)) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_LINK_UNAVAILABLE;
    }

    predicate.local = local;
    predicate.peer = peer;
    ready = wait_condition(&backend->link->condition, &backend->link->mutex,
                           timeout_us, time_code_space_ready, &predicate);
    if (!ready) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return timeout_us == SPW_TIMEOUT_IMMEDIATE
                   ? SPW_ERR_RESOURCE_EXHAUSTED
                   : SPW_ERR_TIMEOUT;
    }
    if (!local->started) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_INVALID_STATE;
    }
    if (!peer_available(peer)) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_LINK_UNAVAILABLE;
    }

    peer->time_codes.slots[peer->time_codes.tail] = *time_code;
    peer->time_codes.tail =
        (peer->time_codes.tail + 1u) % SPW_SIMULATOR_TIME_CODE_QUEUE_DEPTH;
    ++peer->time_codes.count;
    ++local->statistics.tx_time_codes;
    spw_host_condition_broadcast(&backend->link->condition);
    spw_host_mutex_unlock(&backend->link->mutex);
    return SPW_OK;
}

static spw_result_t simulator_receive_time_code(void* context,
                                                spw_time_code_t* time_code,
                                                spw_timeout_us_t timeout_us) {
    spw_simulator_backend_t* backend = (spw_simulator_backend_t*)context;
    spw_simulator_endpoint_state_t* local;
    spw_simulator_endpoint_state_t* peer;
    spw_packet_space_predicate_t predicate;
    bool ready;

    if (backend->link == NULL) {
        return SPW_ERR_INVALID_STATE;
    }

    spw_host_mutex_lock(&backend->link->mutex);
    local = &backend->link->endpoints[backend->endpoint_index];
    peer = &backend->link->endpoints[1u - backend->endpoint_index];
    if (!local->attached || !local->started) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_INVALID_STATE;
    }

    predicate.local = local;
    predicate.peer = peer;
    ready = wait_condition(&backend->link->condition, &backend->link->mutex,
                           timeout_us, time_code_receive_ready, &predicate);
    if (!ready) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_TIMEOUT;
    }
    if (!local->started) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_INVALID_STATE;
    }
    if (local->time_codes.count == 0u) {
        spw_result_t result = !peer_available(peer)
                                  ? SPW_ERR_LINK_UNAVAILABLE
                                  : SPW_ERR_TIMEOUT;
        spw_host_mutex_unlock(&backend->link->mutex);
        return result;
    }

    *time_code = local->time_codes.slots[local->time_codes.head];
    local->time_codes.head =
        (local->time_codes.head + 1u) % SPW_SIMULATOR_TIME_CODE_QUEUE_DEPTH;
    --local->time_codes.count;
    ++local->statistics.rx_time_codes;
    spw_host_condition_broadcast(&backend->link->condition);
    spw_host_mutex_unlock(&backend->link->mutex);
    return SPW_OK;
}

static spw_result_t simulator_get_statistics(
    const void* context,
    spw_statistics_t* out_statistics) {
    const spw_simulator_backend_t* backend =
        (const spw_simulator_backend_t*)context;
    const spw_simulator_endpoint_state_t* local;
    if (backend->link == NULL) {
        return SPW_ERR_INVALID_STATE;
    }
    spw_host_mutex_lock(&backend->link->mutex);
    local = &backend->link->endpoints[backend->endpoint_index];
    if (!local->attached) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    *out_statistics = local->statistics;
    spw_host_mutex_unlock(&backend->link->mutex);
    return SPW_OK;
}

static spw_result_t simulator_clear_statistics(void* context) {
    spw_simulator_backend_t* backend = (spw_simulator_backend_t*)context;
    spw_simulator_endpoint_state_t* local;
    if (backend->link == NULL) {
        return SPW_ERR_INVALID_STATE;
    }
    spw_host_mutex_lock(&backend->link->mutex);
    local = &backend->link->endpoints[backend->endpoint_index];
    if (!local->attached) {
        spw_host_mutex_unlock(&backend->link->mutex);
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    memset(&local->statistics, 0, sizeof(local->statistics));
    spw_host_mutex_unlock(&backend->link->mutex);
    return SPW_OK;
}

static struct spw_buffer* find_free_tx_buffer(spw_simulator_backend_t* backend) {
    size_t i;
    for (i = 0u; i < SPW_SIMULATOR_PACKET_QUEUE_DEPTH; ++i) {
        if (backend->tx_buffers[i].descriptor.state == SPW_BUFFER_STATE_FREE) {
            return &backend->tx_buffers[i].descriptor;
        }
    }
    return NULL;
}

static struct spw_buffer* find_completed_tx_buffer(spw_simulator_backend_t* backend) {
    size_t i;
    for (i = 0u; i < SPW_SIMULATOR_PACKET_QUEUE_DEPTH; ++i) {
        if (backend->tx_buffers[i].descriptor.state == SPW_BUFFER_STATE_COMPLETED) {
            return &backend->tx_buffers[i].descriptor;
        }
    }
    return NULL;
}

static bool free_tx_buffer_ready(void* context) {
    return find_free_tx_buffer((spw_simulator_backend_t*)context) != NULL;
}

static bool completed_tx_buffer_ready(void* context) {
    return find_completed_tx_buffer((spw_simulator_backend_t*)context) != NULL;
}

static bool rx_buffer_ready(void* context) {
    return !((spw_simulator_backend_t*)context)->rx_buffer_acquired;
}

static spw_result_t simulator_acquire_tx_buffer(void* context,
                                                size_t min_capacity,
                                                spw_timeout_us_t timeout_us,
                                                spw_buffer_t** out_buffer) {
    spw_simulator_backend_t* backend = (spw_simulator_backend_t*)context;
    struct spw_buffer* buffer;
    bool ready;
    *out_buffer = NULL;
    if (min_capacity > SPW_SIMULATOR_MAX_PACKET_SIZE) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }

    spw_host_mutex_lock(&backend->zero_copy_mutex);
    buffer = find_free_tx_buffer(backend);
    if (buffer == NULL) {
        ready = wait_condition(&backend->zero_copy_condition,
                               &backend->zero_copy_mutex,
                               timeout_us,
                               free_tx_buffer_ready,
                               backend);
        if (!ready) {
            spw_host_mutex_unlock(&backend->zero_copy_mutex);
            return timeout_us == SPW_TIMEOUT_IMMEDIATE
                       ? SPW_ERR_RESOURCE_EXHAUSTED
                       : SPW_ERR_TIMEOUT;
        }
        buffer = find_free_tx_buffer(backend);
    }
    if (buffer == NULL) {
        spw_host_mutex_unlock(&backend->zero_copy_mutex);
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    buffer->length = 0u;
    buffer->terminator = SPW_TERMINATOR_EOP;
    buffer->state = SPW_BUFFER_STATE_APPLICATION;
    *out_buffer = buffer;
    spw_host_mutex_unlock(&backend->zero_copy_mutex);
    return SPW_OK;
}

static spw_result_t simulator_submit_tx_buffer(void* context,
                                               spw_buffer_t* public_buffer,
                                               spw_timeout_us_t timeout_us) {
    spw_simulator_backend_t* backend = (spw_simulator_backend_t*)context;
    struct spw_buffer* buffer = (struct spw_buffer*)public_buffer;
    spw_packet_t packet;
    spw_result_t result;

    spw_host_mutex_lock(&backend->zero_copy_mutex);
    if (buffer->owner != backend || buffer->direction != SPW_BUFFER_DIRECTION_TX ||
        buffer->state != SPW_BUFFER_STATE_APPLICATION) {
        spw_host_mutex_unlock(&backend->zero_copy_mutex);
        return SPW_ERR_INVALID_STATE;
    }
    if (buffer->length > buffer->capacity ||
        buffer->length > SPW_SIMULATOR_MAX_PACKET_SIZE ||
        !valid_terminator(buffer->terminator)) {
        spw_host_mutex_unlock(&backend->zero_copy_mutex);
        return SPW_ERR_INVALID_PACKET;
    }
    buffer->state = SPW_BUFFER_STATE_BACKEND;
    spw_host_mutex_unlock(&backend->zero_copy_mutex);

    packet.data = buffer->data;
    packet.length = buffer->length;
    packet.capacity = buffer->capacity;
    packet.terminator = buffer->terminator;
    result = simulator_send(backend, &packet, timeout_us);

    spw_host_mutex_lock(&backend->zero_copy_mutex);
    buffer->state = result == SPW_OK
                        ? SPW_BUFFER_STATE_COMPLETED
                        : SPW_BUFFER_STATE_APPLICATION;
    spw_host_condition_broadcast(&backend->zero_copy_condition);
    spw_host_mutex_unlock(&backend->zero_copy_mutex);
    return result;
}

static spw_result_t simulator_reclaim_tx_buffer(void* context,
                                                spw_timeout_us_t timeout_us,
                                                spw_buffer_t** out_buffer) {
    spw_simulator_backend_t* backend = (spw_simulator_backend_t*)context;
    struct spw_buffer* buffer;
    bool ready;
    *out_buffer = NULL;

    spw_host_mutex_lock(&backend->zero_copy_mutex);
    buffer = find_completed_tx_buffer(backend);
    if (buffer == NULL) {
        ready = wait_condition(&backend->zero_copy_condition,
                               &backend->zero_copy_mutex,
                               timeout_us,
                               completed_tx_buffer_ready,
                               backend);
        if (!ready) {
            spw_host_mutex_unlock(&backend->zero_copy_mutex);
            return SPW_ERR_TIMEOUT;
        }
        buffer = find_completed_tx_buffer(backend);
    }
    if (buffer == NULL) {
        spw_host_mutex_unlock(&backend->zero_copy_mutex);
        return SPW_ERR_TIMEOUT;
    }

    buffer->state = SPW_BUFFER_STATE_APPLICATION;
    *out_buffer = buffer;
    spw_host_mutex_unlock(&backend->zero_copy_mutex);
    return SPW_OK;
}

static spw_result_t simulator_release_tx_buffer(void* context,
                                                spw_buffer_t* public_buffer) {
    spw_simulator_backend_t* backend = (spw_simulator_backend_t*)context;
    struct spw_buffer* buffer = (struct spw_buffer*)public_buffer;
    spw_host_mutex_lock(&backend->zero_copy_mutex);
    if (buffer->owner != backend || buffer->direction != SPW_BUFFER_DIRECTION_TX ||
        buffer->state != SPW_BUFFER_STATE_APPLICATION) {
        spw_host_mutex_unlock(&backend->zero_copy_mutex);
        return SPW_ERR_INVALID_STATE;
    }
    buffer->length = 0u;
    buffer->terminator = SPW_TERMINATOR_EOP;
    buffer->state = SPW_BUFFER_STATE_FREE;
    spw_host_condition_broadcast(&backend->zero_copy_condition);
    spw_host_mutex_unlock(&backend->zero_copy_mutex);
    return SPW_OK;
}

static spw_result_t simulator_acquire_rx_buffer(void* context,
                                                spw_timeout_us_t timeout_us,
                                                spw_buffer_t** out_buffer) {
    spw_simulator_backend_t* backend = (spw_simulator_backend_t*)context;
    spw_packet_t packet;
    spw_result_t result;
    bool available;
    *out_buffer = NULL;

    spw_host_mutex_lock(&backend->zero_copy_mutex);
    available = wait_condition(&backend->zero_copy_condition,
                               &backend->zero_copy_mutex,
                               timeout_us,
                               rx_buffer_ready,
                               backend);
    if (!available) {
        spw_host_mutex_unlock(&backend->zero_copy_mutex);
        return timeout_us == SPW_TIMEOUT_IMMEDIATE
                   ? SPW_ERR_RESOURCE_EXHAUSTED
                   : SPW_ERR_TIMEOUT;
    }
    backend->rx_buffer_acquired = true;
    backend->rx_buffer.state = SPW_BUFFER_STATE_BACKEND;
    spw_host_mutex_unlock(&backend->zero_copy_mutex);

    packet.data = backend->rx_storage.bytes;
    packet.length = 0u;
    packet.capacity = SPW_SIMULATOR_MAX_PACKET_SIZE;
    packet.terminator = SPW_TERMINATOR_EOP;
    result = simulator_receive(backend, &packet, timeout_us);

    spw_host_mutex_lock(&backend->zero_copy_mutex);
    if (result != SPW_OK) {
        backend->rx_buffer.state = SPW_BUFFER_STATE_FREE;
        backend->rx_buffer_acquired = false;
        spw_host_condition_broadcast(&backend->zero_copy_condition);
        spw_host_mutex_unlock(&backend->zero_copy_mutex);
        return result;
    }

    backend->rx_buffer.length = packet.length;
    backend->rx_buffer.terminator = packet.terminator;
    backend->rx_buffer.state = SPW_BUFFER_STATE_APPLICATION;
    *out_buffer = &backend->rx_buffer;
    spw_host_mutex_unlock(&backend->zero_copy_mutex);
    return SPW_OK;
}

static spw_result_t simulator_release_rx_buffer(void* context,
                                                spw_buffer_t* public_buffer) {
    spw_simulator_backend_t* backend = (spw_simulator_backend_t*)context;
    struct spw_buffer* buffer = (struct spw_buffer*)public_buffer;
    spw_host_mutex_lock(&backend->zero_copy_mutex);
    if (buffer != &backend->rx_buffer || buffer->owner != backend ||
        buffer->direction != SPW_BUFFER_DIRECTION_RX ||
        buffer->state != SPW_BUFFER_STATE_APPLICATION ||
        !backend->rx_buffer_acquired) {
        spw_host_mutex_unlock(&backend->zero_copy_mutex);
        return SPW_ERR_INVALID_STATE;
    }
    buffer->length = 0u;
    buffer->terminator = SPW_TERMINATOR_EOP;
    buffer->state = SPW_BUFFER_STATE_FREE;
    backend->rx_buffer_acquired = false;
    spw_host_condition_broadcast(&backend->zero_copy_condition);
    spw_host_mutex_unlock(&backend->zero_copy_mutex);
    return SPW_OK;
}

static const spw_backend_ops_t SIMULATOR_OPS = {
    simulator_start,
    simulator_stop,
    simulator_reset,
    simulator_get_link_state,
    simulator_get_capabilities,
    simulator_supports_zero_copy,
    simulator_send,
    simulator_receive,
    simulator_send_time_code,
    simulator_receive_time_code,
    simulator_get_statistics,
    simulator_clear_statistics,
    NULL,
    NULL,
    simulator_acquire_tx_buffer,
    simulator_submit_tx_buffer,
    simulator_reclaim_tx_buffer,
    simulator_release_tx_buffer,
    simulator_acquire_rx_buffer,
    simulator_release_rx_buffer,
    NULL
};

static const spw_backend_factory_t SIMULATOR_FACTORY = {
    sizeof(spw_simulator_backend_t),
    alignof(spw_simulator_backend_t),
    simulator_construct,
    simulator_destroy,
    &SIMULATOR_OPS,
    NULL
};

const spw_backend_factory_t* spw_simulator_backend_factory(void) {
    return &SIMULATOR_FACTORY;
}
