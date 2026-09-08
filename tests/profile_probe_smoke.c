#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <spwkit/spwkit.h>

#include "profiling/profile.h"

typedef struct profile_driver {
    spw_link_state_t state;
    uint8_t payload[8];
    size_t length;
    spw_terminator_t terminator;
    int ready;
    int tx_acquired;
    int tx_submitted;
    int rx_acquired;
} profile_driver_t;

static spw_result_t profile_start(void* raw) {
    profile_driver_t* driver = (profile_driver_t*)raw;
    driver->state = SPW_LINK_RUN;
    return SPW_OK;
}

static spw_result_t profile_stop(void* raw) {
    profile_driver_t* driver = (profile_driver_t*)raw;
    driver->state = SPW_LINK_READY;
    return SPW_OK;
}

static spw_result_t profile_reset(void* raw) {
    profile_driver_t* driver = (profile_driver_t*)raw;
    driver->state = SPW_LINK_ERROR_RESET;
    driver->ready = 0;
    driver->tx_acquired = 0;
    driver->tx_submitted = 0;
    driver->rx_acquired = 0;
    return SPW_OK;
}

static spw_result_t profile_get_link_state(const void* raw,
                                           spw_link_state_t* out_state) {
    const profile_driver_t* driver = (const profile_driver_t*)raw;
    *out_state = driver->state;
    return SPW_OK;
}

static spw_result_t profile_get_capabilities(
    const void* raw,
    spw_capabilities_t* out_capabilities) {
    (void)raw;
    memset(out_capabilities, 0, sizeof(*out_capabilities));
    out_capabilities->max_packet_size = 8u;
    out_capabilities->tx_queue_depth = 1u;
    out_capabilities->rx_queue_depth = 1u;
    out_capabilities->buffer_alignment = 1u;
    return SPW_OK;
}

static spw_result_t profile_send(void* raw,
                                 const spw_packet_t* packet,
                                 spw_timeout_us_t timeout_us) {
    profile_driver_t* driver = (profile_driver_t*)raw;
    (void)timeout_us;

    /* A real provider places this at DMA/MMIO/native submission. */
    SPW_PROFILE_TX_PROVIDER_BOUNDARY();

    if (driver->state != SPW_LINK_RUN || packet->length > sizeof(driver->payload)) {
        return SPW_ERR_INVALID_STATE;
    }
    if (packet->length != 0u) {
        memcpy(driver->payload, packet->data, packet->length);
    }
    driver->length = packet->length;
    driver->terminator = packet->terminator;
    driver->ready = 1;
    return SPW_OK;
}

static spw_result_t profile_receive(void* raw,
                                    spw_packet_t* packet,
                                    spw_timeout_us_t timeout_us) {
    profile_driver_t* driver = (profile_driver_t*)raw;
    (void)timeout_us;

    /* A real provider places this at DMA/native data-ready completion. */
    SPW_PROFILE_RX_PROVIDER_BOUNDARY();

    if (driver->state != SPW_LINK_RUN || !driver->ready) {
        return SPW_ERR_TIMEOUT;
    }
    if (packet->capacity < driver->length) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }
    if (driver->length != 0u) {
        memcpy(packet->data, driver->payload, driver->length);
    }
    packet->length = driver->length;
    packet->terminator = driver->terminator;
    driver->ready = 0;
    return SPW_OK;
}

static void profile_fill_tx_descriptor(profile_driver_t* driver,
                                       spw_driver_buffer_t* out_buffer) {
    out_buffer->data = driver->payload;
    out_buffer->length = driver->length;
    out_buffer->capacity = sizeof(driver->payload);
    out_buffer->terminator = driver->terminator;
    out_buffer->token = 1u;
}

static spw_result_t profile_acquire_tx_buffer(
    void* raw,
    size_t min_capacity,
    spw_timeout_us_t timeout_us,
    spw_driver_buffer_t* out_buffer) {
    profile_driver_t* driver = (profile_driver_t*)raw;
    (void)timeout_us;
    if (driver->state != SPW_LINK_RUN ||
        min_capacity > sizeof(driver->payload) ||
        driver->tx_acquired || driver->tx_submitted) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }
    driver->length = 0u;
    driver->terminator = SPW_TERMINATOR_EOP;
    driver->tx_acquired = 1;
    profile_fill_tx_descriptor(driver, out_buffer);
    return SPW_OK;
}

static spw_result_t profile_submit_tx_buffer(
    void* raw,
    const spw_driver_buffer_t* buffer,
    spw_timeout_us_t timeout_us) {
    profile_driver_t* driver = (profile_driver_t*)raw;
    (void)timeout_us;

    /* Provider-owned equivalent of the DMA/native submission boundary. */
    SPW_PROFILE_TX_ZC_SUBMIT_PROVIDER_BOUNDARY();

    if (!driver->tx_acquired || driver->tx_submitted || buffer->token != 1u ||
        buffer->data != driver->payload || buffer->length > sizeof(driver->payload)) {
        return SPW_ERR_INVALID_STATE;
    }
    driver->length = buffer->length;
    driver->terminator = buffer->terminator;
    driver->tx_acquired = 0;
    driver->tx_submitted = 1;
    return SPW_OK;
}

static spw_result_t profile_reclaim_tx_buffer(
    void* raw,
    spw_timeout_us_t timeout_us,
    spw_driver_buffer_t* out_buffer) {
    profile_driver_t* driver = (profile_driver_t*)raw;
    (void)timeout_us;

    /* Provider-owned completion boundary before ownership is returned. */
    SPW_PROFILE_TX_ZC_RECLAIM_PROVIDER_BOUNDARY();

    if (!driver->tx_submitted || driver->tx_acquired) {
        return SPW_ERR_TIMEOUT;
    }
    driver->tx_submitted = 0;
    driver->tx_acquired = 1;
    profile_fill_tx_descriptor(driver, out_buffer);
    return SPW_OK;
}

static spw_result_t profile_release_tx_buffer(
    void* raw,
    const spw_driver_buffer_t* buffer) {
    profile_driver_t* driver = (profile_driver_t*)raw;
    if (!driver->tx_acquired || driver->tx_submitted || buffer->token != 1u) {
        return SPW_ERR_INVALID_STATE;
    }
    driver->tx_acquired = 0;
    return SPW_OK;
}

static spw_result_t profile_acquire_rx_buffer(
    void* raw,
    spw_timeout_us_t timeout_us,
    spw_driver_buffer_t* out_buffer) {
    profile_driver_t* driver = (profile_driver_t*)raw;
    (void)timeout_us;
    if (driver->state != SPW_LINK_RUN || driver->rx_acquired ||
        out_buffer == NULL) {
        return SPW_ERR_TIMEOUT;
    }

    /* Provider-owned DMA/native data-ready boundary. */
    SPW_PROFILE_RX_ZC_ACQUIRE_PROVIDER_BOUNDARY();

    out_buffer->data = driver->payload;
    out_buffer->length = driver->length;
    out_buffer->capacity = sizeof(driver->payload);
    out_buffer->terminator = driver->terminator;
    out_buffer->token = 2u;
    driver->rx_acquired = 1;
    return SPW_OK;
}

static spw_result_t profile_release_rx_buffer(
    void* raw,
    const spw_driver_buffer_t* buffer) {
    profile_driver_t* driver = (profile_driver_t*)raw;
    if (!driver->rx_acquired || buffer == NULL || buffer->token != 2u ||
        buffer->data != driver->payload) {
        return SPW_ERR_INVALID_STATE;
    }
    driver->rx_acquired = 0;
    return SPW_OK;
}

static spw_result_t profile_sync_buffer(
    void* raw,
    const spw_driver_buffer_t* buffer,
    spw_driver_sync_direction_t direction) {
    (void)raw;
    (void)buffer;
    return (direction == SPW_DRIVER_SYNC_TO_DEVICE ||
            direction == SPW_DRIVER_SYNC_FROM_DEVICE)
               ? SPW_OK
               : SPW_ERR_UNSUPPORTED;
}

static const spw_driver_ops_t PROFILE_OPS = {
    .struct_size = sizeof(spw_driver_ops_t),
    .version = SPW_DRIVER_OPS_VERSION,
    .start = profile_start,
    .stop = profile_stop,
    .reset = profile_reset,
    .get_link_state = profile_get_link_state,
    .get_capabilities = profile_get_capabilities,
    .send = profile_send,
    .receive = profile_receive,
    .acquire_tx_buffer = profile_acquire_tx_buffer,
    .submit_tx_buffer = profile_submit_tx_buffer,
    .reclaim_tx_buffer = profile_reclaim_tx_buffer,
    .release_tx_buffer = profile_release_tx_buffer,
    .acquire_rx_buffer = profile_acquire_rx_buffer,
    .release_rx_buffer = profile_release_rx_buffer,
    .sync_buffer = profile_sync_buffer,
};

static void require_one_sample(void) {
    const volatile spw_profile_sample_t* sample = spw_profile_last_sample();
    assert(sample != NULL);
    assert(sample->sequence == 1u);
    assert(sample->delta == spw_profile_counter_delta(sample->start, sample->end));
}

static void exercise_counter_metadata(void) {
    const uint32_t width = spw_profile_counter_width_bits();

    assert(width == 32u || width == 64u);
    assert(spw_profile_delta32((uint64_t)UINT32_MAX - 3u, 2u) == 6u);
    assert(spw_profile_delta64(10u, 20u) == 10u);

#if SPWKIT_PROFILE_COUNTER_HZ > 0
    assert(spw_profile_counter_frequency_hz() ==
           (uint64_t)SPWKIT_PROFILE_COUNTER_HZ);
#endif
}

static void exercise_probe_mechanism(void) {
    spw_profile_reset();

    SPW_PROFILE_TX_API_ENTRY();
    SPW_PROFILE_TX_BACKEND_ENTRY();
    SPW_PROFILE_TX_PROVIDER_ENTRY();
    SPW_PROFILE_TX_PROVIDER_BOUNDARY();
    SPW_PROFILE_RX_PROVIDER_BOUNDARY();
    SPW_PROFILE_RX_PROVIDER_RETURN();
    SPW_PROFILE_RX_BACKEND_RETURN();
    SPW_PROFILE_RX_API_RETURN();
    SPW_PROFILE_TX_ZC_ACQUIRE_API_ENTRY();
    SPW_PROFILE_TX_ZC_ACQUIRE_PROVIDER_ENTRY();
    SPW_PROFILE_TX_ZC_ACQUIRE_PROVIDER_RETURN();
    SPW_PROFILE_TX_ZC_ACQUIRE_API_RETURN();
    SPW_PROFILE_TX_ZC_SUBMIT_API_ENTRY();
    SPW_PROFILE_TX_ZC_SUBMIT_BACKEND_ENTRY();
    SPW_PROFILE_TX_ZC_SUBMIT_SYNC_ENTRY();
    SPW_PROFILE_TX_ZC_SUBMIT_SYNC_RETURN();
    SPW_PROFILE_TX_ZC_SUBMIT_PROVIDER_ENTRY();
    SPW_PROFILE_TX_ZC_SUBMIT_PROVIDER_BOUNDARY();
    SPW_PROFILE_TX_ZC_SUBMIT_PROVIDER_RETURN();
    SPW_PROFILE_TX_ZC_SUBMIT_BACKEND_RETURN();
    SPW_PROFILE_TX_ZC_SUBMIT_API_RETURN();
    SPW_PROFILE_TX_ZC_RECLAIM_API_ENTRY();
    SPW_PROFILE_TX_ZC_RECLAIM_PROVIDER_ENTRY();
    SPW_PROFILE_TX_ZC_RECLAIM_PROVIDER_BOUNDARY();
    SPW_PROFILE_TX_ZC_RECLAIM_PROVIDER_RETURN();
    SPW_PROFILE_TX_ZC_RECLAIM_BACKEND_RETURN();
    SPW_PROFILE_TX_ZC_RECLAIM_API_RETURN();
    SPW_PROFILE_TX_ZC_RELEASE_API_ENTRY();
    SPW_PROFILE_TX_ZC_RELEASE_BACKEND_ENTRY();
    SPW_PROFILE_TX_ZC_RELEASE_PROVIDER_ENTRY();
    SPW_PROFILE_TX_ZC_RELEASE_PROVIDER_RETURN();
    SPW_PROFILE_TX_ZC_RELEASE_BACKEND_RETURN();
    SPW_PROFILE_TX_ZC_RELEASE_API_RETURN();
    SPW_PROFILE_RX_ZC_ACQUIRE_API_ENTRY();
    SPW_PROFILE_RX_ZC_ACQUIRE_BACKEND_ENTRY();
    SPW_PROFILE_RX_ZC_ACQUIRE_PROVIDER_ENTRY();
    SPW_PROFILE_RX_ZC_ACQUIRE_PROVIDER_BOUNDARY();
    SPW_PROFILE_RX_ZC_ACQUIRE_PROVIDER_RETURN();
    SPW_PROFILE_RX_ZC_ACQUIRE_SYNC_ENTRY();
    SPW_PROFILE_RX_ZC_ACQUIRE_SYNC_RETURN();
    SPW_PROFILE_RX_ZC_ACQUIRE_BACKEND_RETURN();
    SPW_PROFILE_RX_ZC_ACQUIRE_API_RETURN();
    SPW_PROFILE_RX_ZC_RELEASE_API_ENTRY();
    SPW_PROFILE_RX_ZC_RELEASE_BACKEND_ENTRY();
    SPW_PROFILE_RX_ZC_RELEASE_PROVIDER_ENTRY();
    SPW_PROFILE_RX_ZC_RELEASE_PROVIDER_RETURN();
    SPW_PROFILE_RX_ZC_RELEASE_BACKEND_RETURN();
    SPW_PROFILE_RX_ZC_RELEASE_API_RETURN();

    require_one_sample();
}

static void exercise_driver_boundary(void) {
    profile_driver_t driver;
    spw_driver_config_t driver_config =
        SPW_DRIVER_CONFIG_INITIALIZER(&PROFILE_OPS, &driver);
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DRIVER);
    spw_port_t* port = NULL;
    uint8_t tx_data[] = {0x10u, 0x20u, 0x30u, 0x40u};
    uint8_t rx_data[sizeof(tx_data)] = {0u};
    spw_packet_t tx = {
        tx_data, sizeof(tx_data), sizeof(tx_data), SPW_TERMINATOR_EOP};
    spw_packet_t rx = {
        rx_data, 0u, sizeof(rx_data), SPW_TERMINATOR_EOP};

    memset(&driver, 0, sizeof(driver));
    driver.state = SPW_LINK_READY;
    config.backend_config = &driver_config;
    config.backend_config_size = sizeof(driver_config);

    assert(spw_port_open(&config, &port) == SPW_OK);
    assert(spw_port_start(port) == SPW_OK);

#if SPWKIT_PROFILE_START >= SPW_PROFILE_ID_TX_API_ENTRY && \
    SPWKIT_PROFILE_START <= SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY
    spw_profile_reset();
    assert(spw_port_send(port, &tx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    require_one_sample();
#else
    assert(spw_port_send(port, &tx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
#endif

#if SPWKIT_PROFILE_START >= SPW_PROFILE_ID_RX_PROVIDER_BOUNDARY && \
    SPWKIT_PROFILE_START <= SPW_PROFILE_ID_RX_API_RETURN
    spw_profile_reset();
    assert(spw_port_receive(port, &rx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    require_one_sample();
#else
    assert(spw_port_receive(port, &rx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
#endif

    assert(rx.length == sizeof(tx_data));
    assert(memcmp(rx_data, tx_data, sizeof(tx_data)) == 0);
    assert(spw_port_stop(port) == SPW_OK);
    assert(spw_port_close(port) == SPW_OK);
}


static void exercise_zero_copy_boundaries(void) {
    profile_driver_t driver;
    spw_driver_config_t driver_config =
        SPW_DRIVER_CONFIG_INITIALIZER(&PROFILE_OPS, &driver);
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DRIVER);
    spw_port_t* port = NULL;
    spw_buffer_t* buffer = NULL;
    spw_buffer_view_t view;

    memset(&driver, 0, sizeof(driver));
    driver.state = SPW_LINK_READY;
    driver_config.tx_buffer_slots = 1u;
    driver_config.rx_buffer_slots = 1u;
    config.backend_config = &driver_config;
    config.backend_config_size = sizeof(driver_config);

    assert(spw_port_open(&config, &port) == SPW_OK);
    assert(spw_port_start(port) == SPW_OK);

#if SPWKIT_PROFILE_START >= SPW_PROFILE_ID_TX_ZC_ACQUIRE_API_ENTRY && \
    SPWKIT_PROFILE_START <= SPW_PROFILE_ID_TX_ZC_ACQUIRE_API_RETURN
    spw_profile_reset();
    assert(spw_port_acquire_tx_buffer(
               port, 4u, SPW_TIMEOUT_IMMEDIATE, &buffer) == SPW_OK);
    require_one_sample();
#else
    assert(spw_port_acquire_tx_buffer(
               port, 4u, SPW_TIMEOUT_IMMEDIATE, &buffer) == SPW_OK);
#endif

    assert(spw_buffer_get_view(buffer, &view) == SPW_OK);
    assert(view.capacity >= 4u);
    view.data[0] = 0x11u;
    view.data[1] = 0x22u;
    view.data[2] = 0x33u;
    view.data[3] = 0x44u;
    assert(spw_buffer_set_packet(buffer, 4u, SPW_TERMINATOR_EOP) == SPW_OK);

#if SPWKIT_PROFILE_START >= SPW_PROFILE_ID_TX_ZC_SUBMIT_API_ENTRY && \
    SPWKIT_PROFILE_START <= SPW_PROFILE_ID_TX_ZC_SUBMIT_API_RETURN
    spw_profile_reset();
    assert(spw_port_submit_tx_buffer(
               port, &buffer, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    require_one_sample();
#else
    assert(spw_port_submit_tx_buffer(
               port, &buffer, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
#endif
    assert(buffer == NULL);

#if SPWKIT_PROFILE_START >= SPW_PROFILE_ID_TX_ZC_RECLAIM_API_ENTRY && \
    SPWKIT_PROFILE_START <= SPW_PROFILE_ID_TX_ZC_RECLAIM_API_RETURN
    spw_profile_reset();
    assert(spw_port_reclaim_tx_buffer(
               port, SPW_TIMEOUT_IMMEDIATE, &buffer) == SPW_OK);
    require_one_sample();
#else
    assert(spw_port_reclaim_tx_buffer(
               port, SPW_TIMEOUT_IMMEDIATE, &buffer) == SPW_OK);
#endif
    assert(buffer != NULL);

#if SPWKIT_PROFILE_START >= SPW_PROFILE_ID_TX_ZC_RELEASE_API_ENTRY && \
    SPWKIT_PROFILE_START <= SPW_PROFILE_ID_TX_ZC_RELEASE_API_RETURN
    spw_profile_reset();
    assert(spw_port_release_tx_buffer(port, &buffer) == SPW_OK);
    require_one_sample();
#else
    assert(spw_port_release_tx_buffer(port, &buffer) == SPW_OK);
#endif
    assert(buffer == NULL);

#if SPWKIT_PROFILE_START >= SPW_PROFILE_ID_RX_ZC_ACQUIRE_API_ENTRY && \
    SPWKIT_PROFILE_START <= SPW_PROFILE_ID_RX_ZC_ACQUIRE_API_RETURN
    spw_profile_reset();
    assert(spw_port_acquire_rx_buffer(
               port, SPW_TIMEOUT_IMMEDIATE, &buffer) == SPW_OK);
    require_one_sample();
#else
    assert(spw_port_acquire_rx_buffer(
               port, SPW_TIMEOUT_IMMEDIATE, &buffer) == SPW_OK);
#endif
    assert(buffer != NULL);
    assert(spw_buffer_get_view(buffer, &view) == SPW_OK);
    assert(view.length == 4u);
    assert(view.data == driver.payload);

#if SPWKIT_PROFILE_START >= SPW_PROFILE_ID_RX_ZC_RELEASE_API_ENTRY && \
    SPWKIT_PROFILE_START <= SPW_PROFILE_ID_RX_ZC_RELEASE_API_RETURN
    spw_profile_reset();
    assert(spw_port_release_rx_buffer(port, &buffer) == SPW_OK);
    require_one_sample();
#else
    assert(spw_port_release_rx_buffer(port, &buffer) == SPW_OK);
#endif
    assert(buffer == NULL);

    assert(spw_port_stop(port) == SPW_OK);
    assert(spw_port_close(port) == SPW_OK);
}

int main(void) {
    spw_profile_prepare();
    assert(spw_profile_counter_kind() != NULL);

    exercise_counter_metadata();
    exercise_probe_mechanism();
    exercise_driver_boundary();
    exercise_zero_copy_boundaries();
    return 0;
}
