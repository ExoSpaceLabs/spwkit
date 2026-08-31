// SPDX-License-Identifier: Apache-2.0

#include "backends/driver/driver_backend.h"

#include <spwkit/driver.h>

#include "core/buffer_internal.h"

#include <stdbool.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

typedef struct spw_driver_buffer_slot {
    struct spw_buffer buffer;
    spw_driver_buffer_t driver_buffer;
    bool active;
} spw_driver_buffer_slot_t;

typedef struct spw_driver_backend {
    const spw_driver_ops_t* ops;
    void* driver_context;
    size_t tx_slot_count;
    size_t rx_slot_count;
    spw_driver_buffer_slot_t slots[];
} spw_driver_backend_t;

static bool valid_terminator(spw_terminator_t terminator) {
    return terminator == SPW_TERMINATOR_EOP ||
           terminator == SPW_TERMINATOR_EEP;
}

static bool complete_dma_ops(const spw_driver_ops_t* ops) {
    return ops->acquire_tx_buffer != NULL &&
           ops->submit_tx_buffer != NULL &&
           ops->reclaim_tx_buffer != NULL &&
           ops->release_tx_buffer != NULL &&
           ops->acquire_rx_buffer != NULL &&
           ops->release_rx_buffer != NULL;
}

static bool zero_copy_enabled_config(const spw_driver_config_t* config) {
    return complete_dma_ops(config->ops) &&
           config->tx_buffer_slots != 0u &&
           config->rx_buffer_slots != 0u;
}

static spw_result_t driver_context_size_for_config(
    const spw_port_config_t* config,
    size_t* out_context_size) {
    const spw_driver_config_t* driver =
        (const spw_driver_config_t*)config->backend_config;
    size_t total_slots = 0u;
    size_t slots_size = 0u;
    if (out_context_size == NULL || driver == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (zero_copy_enabled_config(driver)) {
        if (driver->tx_buffer_slots > SIZE_MAX - driver->rx_buffer_slots) {
            return SPW_ERR_RESOURCE_EXHAUSTED;
        }
        total_slots = driver->tx_buffer_slots + driver->rx_buffer_slots;
        if (total_slots > SIZE_MAX / sizeof(spw_driver_buffer_slot_t)) {
            return SPW_ERR_RESOURCE_EXHAUSTED;
        }
        slots_size = total_slots * sizeof(spw_driver_buffer_slot_t);
    }
    if (slots_size > SIZE_MAX - sizeof(spw_driver_backend_t)) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }
    *out_context_size = sizeof(spw_driver_backend_t) + slots_size;
    return SPW_OK;
}

static void clear_slot(spw_driver_buffer_slot_t* slot) {
    memset(slot, 0, sizeof(*slot));
    slot->buffer.state = SPW_BUFFER_STATE_FREE;
}

static void clear_slots(spw_driver_backend_t* backend) {
    size_t i;
    for (i = 0u; i < backend->tx_slot_count + backend->rx_slot_count; ++i) {
        clear_slot(&backend->slots[i]);
    }
}

static spw_result_t driver_construct(void* raw,
                                     const spw_port_config_t* config) {
    spw_driver_backend_t* backend = (spw_driver_backend_t*)raw;
    const spw_driver_config_t* driver =
        (const spw_driver_config_t*)config->backend_config;
    const size_t size = sizeof(*backend) +
        (zero_copy_enabled_config(driver)
             ? (driver->tx_buffer_slots + driver->rx_buffer_slots) *
                   sizeof(spw_driver_buffer_slot_t)
             : 0u);
    memset(backend, 0, size);
    backend->ops = driver->ops;
    backend->driver_context = driver->driver_context;
    if (zero_copy_enabled_config(driver)) {
        backend->tx_slot_count = driver->tx_buffer_slots;
        backend->rx_slot_count = driver->rx_buffer_slots;
        clear_slots(backend);
    }
    return SPW_OK;
}

static void driver_destroy(void* raw) {
    spw_driver_backend_t* backend = (spw_driver_backend_t*)raw;
    clear_slots(backend);
    backend->ops = NULL;
    backend->driver_context = NULL;
    backend->tx_slot_count = 0u;
    backend->rx_slot_count = 0u;
}

static spw_result_t driver_start(void* raw) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    return b->ops->start(b->driver_context);
}

static spw_result_t driver_stop(void* raw) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    return b->ops->stop(b->driver_context);
}

static spw_result_t driver_reset(void* raw) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    spw_result_t result = b->ops->reset(b->driver_context);
    if (result == SPW_OK) {
        clear_slots(b);
    }
    return result;
}

static spw_result_t driver_get_link_state(const void* raw,
                                          spw_link_state_t* out_state) {
    const spw_driver_backend_t* b = (const spw_driver_backend_t*)raw;
    return b->ops->get_link_state(b->driver_context, out_state);
}

static spw_result_t driver_get_capabilities(
    const void* raw,
    spw_capabilities_t* out_capabilities) {
    const spw_driver_backend_t* b = (const spw_driver_backend_t*)raw;
    const bool zero_copy = b->tx_slot_count != 0u &&
                           b->rx_slot_count != 0u &&
                           complete_dma_ops(b->ops);
    spw_result_t result =
        b->ops->get_capabilities(b->driver_context, out_capabilities);
    if (result != SPW_OK) {
        return result;
    }
    if ((out_capabilities->bits & SPW_CAP_ZERO_COPY) != 0u && !zero_copy) {
        return SPW_ERR_BACKEND;
    }
    if ((out_capabilities->bits & SPW_CAP_FAULT_INJECTION) != 0u) {
        return SPW_ERR_UNSUPPORTED;
    }
    if ((out_capabilities->bits & SPW_CAP_TIME_CODE) != 0u &&
        (b->ops->send_time_code == NULL ||
         b->ops->receive_time_code == NULL)) {
        return SPW_ERR_BACKEND;
    }
    if ((out_capabilities->bits & SPW_CAP_STATISTICS) != 0u &&
        (b->ops->get_statistics == NULL ||
         b->ops->clear_statistics == NULL)) {
        return SPW_ERR_BACKEND;
    }
    if ((out_capabilities->bits & SPW_CAP_READINESS) != 0u &&
        b->ops->wait == NULL) {
        return SPW_ERR_BACKEND;
    }
    if (b->ops->wait == NULL) {
        out_capabilities->bits &= ~SPW_CAP_READINESS;
    }
    return SPW_OK;
}

static bool driver_supports_zero_copy(const void* raw) {
    const spw_driver_backend_t* b = (const spw_driver_backend_t*)raw;
    return b->tx_slot_count != 0u && b->rx_slot_count != 0u &&
           complete_dma_ops(b->ops);
}

static spw_result_t driver_send(void* raw,
                                const spw_packet_t* packet,
                                spw_timeout_us_t timeout_us) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    return b->ops->send(b->driver_context, packet, timeout_us);
}

static spw_result_t driver_receive(void* raw,
                                   spw_packet_t* packet,
                                   spw_timeout_us_t timeout_us) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    return b->ops->receive(b->driver_context, packet, timeout_us);
}

static spw_result_t driver_send_time_code(void* raw,
                                          const spw_time_code_t* code,
                                          spw_timeout_us_t timeout_us) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    return b->ops->send_time_code != NULL
               ? b->ops->send_time_code(b->driver_context, code, timeout_us)
               : SPW_ERR_UNSUPPORTED;
}

static spw_result_t driver_receive_time_code(void* raw,
                                             spw_time_code_t* code,
                                             spw_timeout_us_t timeout_us) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    return b->ops->receive_time_code != NULL
               ? b->ops->receive_time_code(b->driver_context, code, timeout_us)
               : SPW_ERR_UNSUPPORTED;
}

static spw_result_t driver_get_statistics(
    const void* raw,
    spw_statistics_t* out_statistics) {
    const spw_driver_backend_t* b = (const spw_driver_backend_t*)raw;
    if (b->ops->get_statistics == NULL) {
        memset(out_statistics, 0, sizeof(*out_statistics));
        return SPW_ERR_UNSUPPORTED;
    }
    return b->ops->get_statistics(b->driver_context, out_statistics);
}

static spw_result_t driver_clear_statistics(void* raw) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    return b->ops->clear_statistics != NULL
               ? b->ops->clear_statistics(b->driver_context)
               : SPW_ERR_UNSUPPORTED;
}

static spw_result_t driver_wait(void* raw,
                                spw_ready_events_t interests,
                                spw_timeout_us_t timeout_us,
                                spw_ready_events_t* out_ready) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    return b->ops->wait != NULL
               ? b->ops->wait(b->driver_context,
                              interests,
                              timeout_us,
                              out_ready)
               : SPW_ERR_UNSUPPORTED;
}

static spw_driver_buffer_slot_t* find_free_slot(
    spw_driver_backend_t* b,
    size_t first,
    size_t count) {
    size_t i;
    for (i = first; i < first + count; ++i) {
        if (!b->slots[i].active) {
            return &b->slots[i];
        }
    }
    return NULL;
}

static bool token_in_use(const spw_driver_backend_t* b,
                         spw_driver_buffer_token_t token) {
    size_t i;
    for (i = 0u; i < b->tx_slot_count + b->rx_slot_count; ++i) {
        if (b->slots[i].active &&
            b->slots[i].driver_buffer.token == token) {
            return true;
        }
    }
    return false;
}

static spw_driver_buffer_slot_t* find_slot_for_public_buffer(
    spw_driver_backend_t* b,
    spw_buffer_t* buffer,
    spw_buffer_direction_internal_t direction) {
    size_t i;
    for (i = 0u; i < b->tx_slot_count + b->rx_slot_count; ++i) {
        spw_driver_buffer_slot_t* slot = &b->slots[i];
        if (slot->active && &slot->buffer == (struct spw_buffer*)buffer &&
            slot->buffer.owner == b && slot->buffer.direction == direction) {
            return slot;
        }
    }
    return NULL;
}

static spw_driver_buffer_slot_t* find_tx_slot_for_token(
    spw_driver_backend_t* b,
    spw_driver_buffer_token_t token) {
    size_t i;
    for (i = 0u; i < b->tx_slot_count; ++i) {
        if (b->slots[i].active &&
            b->slots[i].driver_buffer.token == token &&
            b->slots[i].buffer.state == SPW_BUFFER_STATE_BACKEND) {
            return &b->slots[i];
        }
    }
    return NULL;
}

static spw_result_t validate_driver_buffer(
    const spw_driver_buffer_t* buffer,
    bool require_packet_metadata) {
    if (buffer->length > buffer->capacity ||
        (buffer->capacity != 0u && buffer->data == NULL)) {
        return SPW_ERR_BACKEND;
    }
    if (require_packet_metadata && !valid_terminator(buffer->terminator)) {
        return SPW_ERR_BACKEND;
    }
    return SPW_OK;
}

static void expose_slot(spw_driver_backend_t* b,
                        spw_driver_buffer_slot_t* slot,
                        spw_buffer_direction_internal_t direction,
                        spw_buffer_state_internal_t state) {
    slot->buffer.data = slot->driver_buffer.data;
    slot->buffer.length = slot->driver_buffer.length;
    slot->buffer.capacity = slot->driver_buffer.capacity;
    slot->buffer.terminator = slot->driver_buffer.terminator;
    slot->buffer.owner = b;
    slot->buffer.direction = direction;
    slot->buffer.state = state;
    slot->buffer.token = (size_t)(slot - b->slots);
    slot->active = true;
}

static spw_result_t driver_acquire_tx_buffer(void* raw,
                                             size_t min_capacity,
                                             spw_timeout_us_t timeout_us,
                                             spw_buffer_t** out_buffer) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    spw_driver_buffer_slot_t* slot =
        find_free_slot(b, 0u, b->tx_slot_count);
    spw_driver_buffer_t descriptor;
    spw_result_t result;
    if (slot == NULL) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }
    memset(&descriptor, 0, sizeof(descriptor));
    result = b->ops->acquire_tx_buffer(
        b->driver_context, min_capacity, timeout_us, &descriptor);
    if (result != SPW_OK) {
        return result;
    }
    if (validate_driver_buffer(&descriptor, false) != SPW_OK ||
        descriptor.capacity < min_capacity ||
        token_in_use(b, descriptor.token)) {
        (void)b->ops->release_tx_buffer(b->driver_context, &descriptor);
        return SPW_ERR_BACKEND;
    }
    descriptor.length = 0u;
    descriptor.terminator = SPW_TERMINATOR_EOP;
    slot->driver_buffer = descriptor;
    expose_slot(b, slot, SPW_BUFFER_DIRECTION_TX,
                SPW_BUFFER_STATE_APPLICATION);
    *out_buffer = (spw_buffer_t*)&slot->buffer;
    return SPW_OK;
}

static spw_result_t driver_submit_tx_buffer(void* raw,
                                            spw_buffer_t* buffer,
                                            spw_timeout_us_t timeout_us) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    spw_driver_buffer_slot_t* slot = find_slot_for_public_buffer(
        b, buffer, SPW_BUFFER_DIRECTION_TX);
    spw_result_t result;
    if (slot == NULL || slot->buffer.state != SPW_BUFFER_STATE_APPLICATION) {
        return SPW_ERR_INVALID_STATE;
    }
    slot->driver_buffer.length = slot->buffer.length;
    slot->driver_buffer.terminator = slot->buffer.terminator;
    if (!valid_terminator(slot->driver_buffer.terminator) ||
        slot->driver_buffer.length > slot->driver_buffer.capacity) {
        return SPW_ERR_INVALID_PACKET;
    }
    if (b->ops->sync_buffer != NULL) {
        result = b->ops->sync_buffer(b->driver_context,
                                     &slot->driver_buffer,
                                     SPW_DRIVER_SYNC_TO_DEVICE);
        if (result != SPW_OK) {
            return result;
        }
    }
    result = b->ops->submit_tx_buffer(
        b->driver_context, &slot->driver_buffer, timeout_us);
    if (result == SPW_OK) {
        slot->buffer.state = SPW_BUFFER_STATE_BACKEND;
    }
    return result;
}

static spw_result_t driver_reclaim_tx_buffer(void* raw,
                                             spw_timeout_us_t timeout_us,
                                             spw_buffer_t** out_buffer) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    spw_driver_buffer_t descriptor;
    spw_driver_buffer_slot_t* slot;
    spw_result_t result;
    memset(&descriptor, 0, sizeof(descriptor));
    result = b->ops->reclaim_tx_buffer(
        b->driver_context, timeout_us, &descriptor);
    if (result != SPW_OK) {
        return result;
    }
    slot = find_tx_slot_for_token(b, descriptor.token);
    if (slot == NULL || validate_driver_buffer(&descriptor, false) != SPW_OK) {
        return SPW_ERR_BACKEND;
    }
    slot->driver_buffer = descriptor;
    expose_slot(b, slot, SPW_BUFFER_DIRECTION_TX,
                SPW_BUFFER_STATE_APPLICATION);
    *out_buffer = (spw_buffer_t*)&slot->buffer;
    return SPW_OK;
}

static spw_result_t driver_release_tx_buffer(void* raw,
                                             spw_buffer_t* buffer) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    spw_driver_buffer_slot_t* slot = find_slot_for_public_buffer(
        b, buffer, SPW_BUFFER_DIRECTION_TX);
    spw_result_t result;
    if (slot == NULL || slot->buffer.state != SPW_BUFFER_STATE_APPLICATION) {
        return SPW_ERR_INVALID_STATE;
    }
    slot->driver_buffer.length = slot->buffer.length;
    slot->driver_buffer.terminator = slot->buffer.terminator;
    result = b->ops->release_tx_buffer(
        b->driver_context, &slot->driver_buffer);
    if (result == SPW_OK) {
        clear_slot(slot);
    }
    return result;
}

static spw_result_t driver_acquire_rx_buffer(void* raw,
                                             spw_timeout_us_t timeout_us,
                                             spw_buffer_t** out_buffer) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    spw_driver_buffer_slot_t* slot = find_free_slot(
        b, b->tx_slot_count, b->rx_slot_count);
    spw_driver_buffer_t descriptor;
    spw_result_t result;
    if (slot == NULL) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }
    memset(&descriptor, 0, sizeof(descriptor));
    result = b->ops->acquire_rx_buffer(
        b->driver_context, timeout_us, &descriptor);
    if (result != SPW_OK) {
        return result;
    }
    if (validate_driver_buffer(&descriptor, true) != SPW_OK ||
        token_in_use(b, descriptor.token)) {
        (void)b->ops->release_rx_buffer(b->driver_context, &descriptor);
        return SPW_ERR_BACKEND;
    }
    if (b->ops->sync_buffer != NULL) {
        result = b->ops->sync_buffer(b->driver_context,
                                     &descriptor,
                                     SPW_DRIVER_SYNC_FROM_DEVICE);
        if (result != SPW_OK) {
            (void)b->ops->release_rx_buffer(b->driver_context, &descriptor);
            return result;
        }
    }
    slot->driver_buffer = descriptor;
    expose_slot(b, slot, SPW_BUFFER_DIRECTION_RX,
                SPW_BUFFER_STATE_APPLICATION);
    *out_buffer = (spw_buffer_t*)&slot->buffer;
    return SPW_OK;
}

static spw_result_t driver_release_rx_buffer(void* raw,
                                             spw_buffer_t* buffer) {
    spw_driver_backend_t* b = (spw_driver_backend_t*)raw;
    spw_driver_buffer_slot_t* slot = find_slot_for_public_buffer(
        b, buffer, SPW_BUFFER_DIRECTION_RX);
    spw_result_t result;
    if (slot == NULL || slot->buffer.state != SPW_BUFFER_STATE_APPLICATION) {
        return SPW_ERR_INVALID_STATE;
    }
    result = b->ops->release_rx_buffer(
        b->driver_context, &slot->driver_buffer);
    if (result == SPW_OK) {
        clear_slot(slot);
    }
    return result;
}

#define DRIVER_COMMON_OPS(tx_acq_, tx_submit_, tx_reclaim_, tx_release_, \
                          rx_acq_, rx_release_, wait_) \
    { driver_start, driver_stop, driver_reset, driver_get_link_state, \
      driver_get_capabilities, driver_supports_zero_copy, driver_send, \
      driver_receive, driver_send_time_code, driver_receive_time_code, \
      driver_get_statistics, driver_clear_statistics, NULL, NULL, \
      (tx_acq_), (tx_submit_), (tx_reclaim_), (tx_release_), (rx_acq_), \
      (rx_release_), (wait_) }

static const spw_backend_ops_t DRIVER_OPS =
    DRIVER_COMMON_OPS(NULL, NULL, NULL, NULL, NULL, NULL, NULL);
static const spw_backend_ops_t DRIVER_WAIT_OPS =
    DRIVER_COMMON_OPS(NULL, NULL, NULL, NULL, NULL, NULL, driver_wait);
static const spw_backend_ops_t DRIVER_DMA_OPS = DRIVER_COMMON_OPS(
    driver_acquire_tx_buffer, driver_submit_tx_buffer,
    driver_reclaim_tx_buffer, driver_release_tx_buffer,
    driver_acquire_rx_buffer, driver_release_rx_buffer, NULL);
static const spw_backend_ops_t DRIVER_DMA_WAIT_OPS = DRIVER_COMMON_OPS(
    driver_acquire_tx_buffer, driver_submit_tx_buffer,
    driver_reclaim_tx_buffer, driver_release_tx_buffer,
    driver_acquire_rx_buffer, driver_release_rx_buffer, driver_wait);

#define DRIVER_FACTORY_WITH_OPS(ops_) \
    { sizeof(spw_driver_backend_t), alignof(spw_driver_backend_t), \
      driver_construct, driver_destroy, (ops_), \
      driver_context_size_for_config }

static const spw_backend_factory_t DRIVER_FACTORY =
    DRIVER_FACTORY_WITH_OPS(&DRIVER_OPS);
static const spw_backend_factory_t DRIVER_WAIT_FACTORY =
    DRIVER_FACTORY_WITH_OPS(&DRIVER_WAIT_OPS);
static const spw_backend_factory_t DRIVER_DMA_FACTORY =
    DRIVER_FACTORY_WITH_OPS(&DRIVER_DMA_OPS);
static const spw_backend_factory_t DRIVER_DMA_WAIT_FACTORY =
    DRIVER_FACTORY_WITH_OPS(&DRIVER_DMA_WAIT_OPS);

const spw_backend_factory_t* spw_driver_backend_factory(
    bool enable_wait,
    bool enable_zero_copy) {
    if (enable_zero_copy) {
        return enable_wait ? &DRIVER_DMA_WAIT_FACTORY : &DRIVER_DMA_FACTORY;
    }
    return enable_wait ? &DRIVER_WAIT_FACTORY : &DRIVER_FACTORY;
}
