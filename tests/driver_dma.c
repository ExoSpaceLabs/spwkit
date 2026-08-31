// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { DMA_SLOTS = 2, DMA_CAPACITY = 64 };

typedef enum fake_dma_state {
    DMA_FREE = 0,
    DMA_TX_APP,
    DMA_TX_COMPLETED,
    DMA_RX_READY,
    DMA_RX_APP
} fake_dma_state_t;

typedef struct fake_dma_slot {
    uint8_t bytes[DMA_CAPACITY];
    size_t length;
    spw_terminator_t terminator;
    spw_driver_buffer_token_t token;
    fake_dma_state_t state;
} fake_dma_slot_t;

typedef struct fake_dma_driver {
    spw_link_state_t state;
    fake_dma_slot_t tx[DMA_SLOTS];
    fake_dma_slot_t rx[DMA_SLOTS];
    unsigned sync_to_device;
    unsigned sync_from_device;
} fake_dma_driver_t;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

static spw_result_t start(void* raw) {
    ((fake_dma_driver_t*)raw)->state = SPW_LINK_RUN;
    return SPW_OK;
}
static spw_result_t stop(void* raw) {
    ((fake_dma_driver_t*)raw)->state = SPW_LINK_READY;
    return SPW_OK;
}
static spw_result_t reset(void* raw) {
    fake_dma_driver_t* d = (fake_dma_driver_t*)raw;
    size_t i;
    d->state = SPW_LINK_ERROR_RESET;
    for (i = 0u; i < DMA_SLOTS; ++i) {
        d->tx[i].state = DMA_FREE;
        d->rx[i].state = DMA_FREE;
    }
    return SPW_OK;
}
static spw_result_t get_state(const void* raw, spw_link_state_t* out) {
    *out = ((const fake_dma_driver_t*)raw)->state;
    return SPW_OK;
}
static spw_result_t get_caps(const void* raw, spw_capabilities_t* out) {
    (void)raw;
    memset(out, 0, sizeof(*out));
    out->bits = SPW_CAP_EEP | SPW_CAP_LINK_CONTROL | SPW_CAP_ZERO_COPY;
    out->max_packet_size = DMA_CAPACITY;
    out->tx_queue_depth = DMA_SLOTS;
    out->rx_queue_depth = DMA_SLOTS;
    out->buffer_alignment = 1u;
    return SPW_OK;
}
static spw_result_t copied_send(void* raw, const spw_packet_t* p,
                                spw_timeout_us_t t) {
    (void)raw; (void)p; (void)t;
    return SPW_ERR_UNSUPPORTED;
}
static spw_result_t copied_receive(void* raw, spw_packet_t* p,
                                   spw_timeout_us_t t) {
    (void)raw; (void)p; (void)t;
    return SPW_ERR_UNSUPPORTED;
}

static spw_driver_buffer_t descriptor(fake_dma_slot_t* slot) {
    spw_driver_buffer_t out;
    out.data = slot->bytes;
    out.length = slot->length;
    out.capacity = sizeof(slot->bytes);
    out.terminator = slot->terminator;
    out.token = slot->token;
    return out;
}

static fake_dma_slot_t* find_token(fake_dma_slot_t slots[DMA_SLOTS],
                                   spw_driver_buffer_token_t token) {
    size_t i;
    for (i = 0u; i < DMA_SLOTS; ++i) {
        if (slots[i].token == token) {
            return &slots[i];
        }
    }
    return NULL;
}

static spw_result_t acquire_tx(void* raw, size_t min_capacity,
                               spw_timeout_us_t timeout_us,
                               spw_driver_buffer_t* out) {
    fake_dma_driver_t* d = (fake_dma_driver_t*)raw;
    size_t i;
    (void)timeout_us;
    if (min_capacity > DMA_CAPACITY) return SPW_ERR_RESOURCE_EXHAUSTED;
    for (i = 0u; i < DMA_SLOTS; ++i) {
        if (d->tx[i].state == DMA_FREE) {
            d->tx[i].state = DMA_TX_APP;
            d->tx[i].length = 0u;
            d->tx[i].terminator = SPW_TERMINATOR_EOP;
            *out = descriptor(&d->tx[i]);
            return SPW_OK;
        }
    }
    return SPW_ERR_RESOURCE_EXHAUSTED;
}

static spw_result_t submit_tx(void* raw, const spw_driver_buffer_t* in,
                              spw_timeout_us_t timeout_us) {
    fake_dma_driver_t* d = (fake_dma_driver_t*)raw;
    fake_dma_slot_t* slot = find_token(d->tx, in->token);
    (void)timeout_us;
    if (slot == NULL || slot->state != DMA_TX_APP ||
        in->data != slot->bytes || in->capacity != DMA_CAPACITY) {
        return SPW_ERR_BACKEND;
    }
    slot->length = in->length;
    slot->terminator = in->terminator;
    slot->state = DMA_TX_COMPLETED;
    return SPW_OK;
}

static spw_result_t reclaim_tx(void* raw, spw_timeout_us_t timeout_us,
                               spw_driver_buffer_t* out) {
    fake_dma_driver_t* d = (fake_dma_driver_t*)raw;
    size_t i;
    (void)timeout_us;
    for (i = 0u; i < DMA_SLOTS; ++i) {
        if (d->tx[i].state == DMA_TX_COMPLETED) {
            d->tx[i].state = DMA_TX_APP;
            *out = descriptor(&d->tx[i]);
            return SPW_OK;
        }
    }
    return SPW_ERR_TIMEOUT;
}

static spw_result_t release_tx(void* raw, const spw_driver_buffer_t* in) {
    fake_dma_driver_t* d = (fake_dma_driver_t*)raw;
    fake_dma_slot_t* slot = find_token(d->tx, in->token);
    if (slot == NULL || slot->state != DMA_TX_APP) return SPW_ERR_BACKEND;
    slot->state = DMA_FREE;
    return SPW_OK;
}

static spw_result_t acquire_rx(void* raw, spw_timeout_us_t timeout_us,
                               spw_driver_buffer_t* out) {
    fake_dma_driver_t* d = (fake_dma_driver_t*)raw;
    size_t i;
    (void)timeout_us;
    for (i = 0u; i < DMA_SLOTS; ++i) {
        if (d->rx[i].state == DMA_RX_READY) {
            d->rx[i].state = DMA_RX_APP;
            *out = descriptor(&d->rx[i]);
            return SPW_OK;
        }
    }
    return SPW_ERR_TIMEOUT;
}

static spw_result_t release_rx(void* raw, const spw_driver_buffer_t* in) {
    fake_dma_driver_t* d = (fake_dma_driver_t*)raw;
    fake_dma_slot_t* slot = find_token(d->rx, in->token);
    if (slot == NULL || slot->state != DMA_RX_APP) return SPW_ERR_BACKEND;
    slot->state = DMA_FREE;
    return SPW_OK;
}

static spw_result_t sync_buffer(void* raw,
                                const spw_driver_buffer_t* buffer,
                                spw_driver_sync_direction_t direction) {
    fake_dma_driver_t* d = (fake_dma_driver_t*)raw;
    (void)buffer;
    if (direction == SPW_DRIVER_SYNC_TO_DEVICE) {
        ++d->sync_to_device;
    } else if (direction == SPW_DRIVER_SYNC_FROM_DEVICE) {
        ++d->sync_from_device;
    } else {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    return SPW_OK;
}

static const spw_driver_ops_t OPS = {
    sizeof(spw_driver_ops_t), SPW_DRIVER_OPS_VERSION,
    start, stop, reset, get_state, get_caps,
    copied_send, copied_receive,
    NULL, NULL, NULL, NULL, NULL,
    acquire_tx, submit_tx, reclaim_tx, release_tx,
    acquire_rx, release_rx, sync_buffer
};

static const spw_driver_ops_t PARTIAL_OPS = {
    sizeof(spw_driver_ops_t), SPW_DRIVER_OPS_VERSION,
    start, stop, reset, get_state, get_caps,
    copied_send, copied_receive,
    NULL, NULL, NULL, NULL, NULL,
    acquire_tx, NULL, NULL, NULL, NULL, NULL, NULL
};

typedef union workspace_storage {
    long double align_long_double;
    void* align_pointer;
    uint64_t align_integer;
    unsigned char bytes[8192];
} workspace_storage_t;

int main(void) {
    fake_dma_driver_t driver;
    spw_driver_config_t driver_config =
        SPW_DRIVER_CONFIG_INITIALIZER(&OPS, &driver);
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DRIVER);
    spw_port_workspace_requirements_t with_dma;
    spw_port_workspace_requirements_t without_dma;
    workspace_storage_t workspace;
    spw_port_t* port = NULL;
    spw_capabilities_t caps;
    spw_buffer_t* tx = NULL;
    spw_buffer_t* reclaimed = NULL;
    spw_buffer_t* rx = NULL;
    spw_buffer_t* stale = NULL;
    spw_buffer_view_t view;
    const uint8_t expected_rx[] = {0xa1u, 0xb2u, 0xc3u, 0xd4u};
    spw_driver_config_t partial =
        SPW_DRIVER_CONFIG_INITIALIZER(&PARTIAL_OPS, &driver);
    spw_port_config_t partial_config =
        SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DRIVER);
    size_t i;

    memset(&driver, 0, sizeof(driver));
    driver.state = SPW_LINK_ERROR_RESET;
    for (i = 0u; i < DMA_SLOTS; ++i) {
        driver.tx[i].token = UINT64_C(100) + i;
        driver.tx[i].state = DMA_FREE;
        driver.rx[i].token = UINT64_C(200) + i;
        driver.rx[i].state = DMA_FREE;
    }

    config.backend_config = &driver_config;
    config.backend_config_size = sizeof(driver_config);
    CHECK(spw_port_workspace_requirements(&config, &without_dma) == SPW_OK);

    driver_config.tx_buffer_slots = DMA_SLOTS;
    driver_config.rx_buffer_slots = DMA_SLOTS;
    CHECK(spw_port_workspace_requirements(&config, &with_dma) == SPW_OK);
    CHECK(with_dma.size > without_dma.size);
    CHECK(with_dma.size <= sizeof(workspace.bytes));

    partial.tx_buffer_slots = 1u;
    partial.rx_buffer_slots = 1u;
    partial_config.backend_config = &partial;
    partial_config.backend_config_size = sizeof(partial);
    CHECK(spw_port_workspace_requirements(&partial_config, &without_dma) ==
          SPW_ERR_INVALID_ARGUMENT);

    CHECK(spw_port_open_in_place(&config, workspace.bytes,
                                 sizeof(workspace.bytes), &port) == SPW_OK);
    CHECK(spw_port_start(port) == SPW_OK);
    CHECK(spw_port_get_capabilities(port, &caps) == SPW_OK);
    CHECK((caps.bits & SPW_CAP_ZERO_COPY) != 0u);

    CHECK(spw_port_acquire_tx_buffer(port, 16u, SPW_TIMEOUT_IMMEDIATE,
                                     &tx) == SPW_OK);
    CHECK(spw_buffer_get_view(tx, &view) == SPW_OK);
    CHECK(view.data == driver.tx[0].bytes);
    CHECK(view.capacity == DMA_CAPACITY);
    view.data[0] = 0x11u;
    view.data[1] = 0x22u;
    view.data[2] = 0x33u;
    CHECK(spw_buffer_set_packet(tx, 3u, SPW_TERMINATOR_EEP) == SPW_OK);
    CHECK(spw_port_submit_tx_buffer(port, &tx,
                                    SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    CHECK(tx == NULL);
    CHECK(driver.sync_to_device == 1u);
    CHECK(driver.tx[0].length == 3u);
    CHECK(driver.tx[0].terminator == SPW_TERMINATOR_EEP);
    CHECK(driver.tx[0].bytes[1] == 0x22u);

    CHECK(spw_port_reclaim_tx_buffer(port, SPW_TIMEOUT_IMMEDIATE,
                                     &reclaimed) == SPW_OK);
    CHECK(spw_buffer_get_view(reclaimed, &view) == SPW_OK);
    CHECK(view.data == driver.tx[0].bytes);
    CHECK(spw_port_release_tx_buffer(port, &reclaimed) == SPW_OK);
    CHECK(reclaimed == NULL);
    CHECK(driver.tx[0].state == DMA_FREE);

    memcpy(driver.rx[0].bytes, expected_rx, sizeof(expected_rx));
    driver.rx[0].length = sizeof(expected_rx);
    driver.rx[0].terminator = SPW_TERMINATOR_EOP;
    driver.rx[0].state = DMA_RX_READY;
    CHECK(spw_port_acquire_rx_buffer(port, SPW_TIMEOUT_IMMEDIATE,
                                     &rx) == SPW_OK);
    CHECK(driver.sync_from_device == 1u);
    CHECK(spw_buffer_get_view(rx, &view) == SPW_OK);
    CHECK(view.data == driver.rx[0].bytes);
    CHECK(view.length == sizeof(expected_rx));
    CHECK(memcmp(view.data, expected_rx, sizeof(expected_rx)) == 0);
    CHECK(spw_port_release_rx_buffer(port, &rx) == SPW_OK);
    CHECK(rx == NULL);
    CHECK(driver.rx[0].state == DMA_FREE);

    CHECK(spw_port_acquire_tx_buffer(port, 8u, SPW_TIMEOUT_IMMEDIATE,
                                     &stale) == SPW_OK);
    CHECK(spw_port_reset(port) == SPW_OK);
    CHECK(spw_buffer_get_view(stale, &view) == SPW_ERR_INVALID_STATE);
    CHECK(spw_port_close(port) == SPW_OK);
    return 0;
}
