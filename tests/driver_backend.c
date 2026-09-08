// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct fake_driver {
    spw_link_state_t state;
    bool advertise_readiness;
    bool packet_ready;
    bool time_code_ready;
    uint8_t packet[256];
    size_t packet_length;
    spw_terminator_t packet_terminator;
    spw_time_code_t time_code;
    spw_statistics_t statistics;
} fake_driver_t;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #expr); \
            return 1; \
        } \
    } while (0)

static spw_result_t fake_start(void* raw) {
    fake_driver_t* d = (fake_driver_t*)raw;
    d->state = SPW_LINK_RUN;
    return SPW_OK;
}

static spw_result_t fake_stop(void* raw) {
    fake_driver_t* d = (fake_driver_t*)raw;
    d->state = SPW_LINK_READY;
    return SPW_OK;
}

static spw_result_t fake_reset(void* raw) {
    fake_driver_t* d = (fake_driver_t*)raw;
    d->state = SPW_LINK_ERROR_RESET;
    d->packet_ready = false;
    d->time_code_ready = false;
    return SPW_OK;
}

static spw_result_t fake_get_link_state(const void* raw,
                                        spw_link_state_t* out_state) {
    const fake_driver_t* d = (const fake_driver_t*)raw;
    *out_state = d->state;
    return SPW_OK;
}

static spw_result_t fake_get_capabilities(
    const void* raw,
    spw_capabilities_t* out_capabilities) {
    const fake_driver_t* d = (const fake_driver_t*)raw;
    memset(out_capabilities, 0, sizeof(*out_capabilities));
    out_capabilities->bits = SPW_CAP_EEP | SPW_CAP_TIME_CODE |
                             SPW_CAP_LINK_CONTROL | SPW_CAP_STATISTICS;
    if (d->advertise_readiness) {
        out_capabilities->bits |= SPW_CAP_READINESS;
    }
    out_capabilities->max_packet_size = sizeof(d->packet);
    out_capabilities->tx_queue_depth = 1u;
    out_capabilities->rx_queue_depth = 1u;
    out_capabilities->buffer_alignment = 1u;
    return SPW_OK;
}

static spw_result_t fake_send(void* raw,
                              const spw_packet_t* packet,
                              spw_timeout_us_t timeout_us) {
    fake_driver_t* d = (fake_driver_t*)raw;
    (void)timeout_us;
    if (d->state != SPW_LINK_RUN) {
        return SPW_ERR_INVALID_STATE;
    }
    if (d->packet_ready) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }
    if (packet->length > sizeof(d->packet)) {
        return SPW_ERR_INVALID_PACKET;
    }
    if (packet->length != 0u && packet->data == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (packet->length != 0u) {
        memcpy(d->packet, packet->data, packet->length);
    }
    d->packet_length = packet->length;
    d->packet_terminator = packet->terminator;
    d->packet_ready = true;
    ++d->statistics.tx_packets;
    d->statistics.tx_bytes += packet->length;
    if (packet->terminator == SPW_TERMINATOR_EEP) {
        ++d->statistics.eep_packets;
    }
    return SPW_OK;
}

static spw_result_t fake_receive(void* raw,
                                 spw_packet_t* packet,
                                 spw_timeout_us_t timeout_us) {
    fake_driver_t* d = (fake_driver_t*)raw;
    (void)timeout_us;
    if (d->state != SPW_LINK_RUN) {
        return SPW_ERR_INVALID_STATE;
    }
    if (!d->packet_ready) {
        return SPW_ERR_TIMEOUT;
    }
    packet->length = d->packet_length;
    packet->terminator = d->packet_terminator;
    if (packet->capacity < d->packet_length) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }
    if (d->packet_length != 0u && packet->data == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (d->packet_length != 0u) {
        memcpy(packet->data, d->packet, d->packet_length);
    }
    d->packet_ready = false;
    ++d->statistics.rx_packets;
    d->statistics.rx_bytes += d->packet_length;
    return SPW_OK;
}

static spw_result_t fake_send_time_code(void* raw,
                                        const spw_time_code_t* code,
                                        spw_timeout_us_t timeout_us) {
    fake_driver_t* d = (fake_driver_t*)raw;
    (void)timeout_us;
    if (d->state != SPW_LINK_RUN) {
        return SPW_ERR_INVALID_STATE;
    }
    if (d->time_code_ready) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }
    d->time_code = *code;
    d->time_code_ready = true;
    ++d->statistics.tx_time_codes;
    return SPW_OK;
}

static spw_result_t fake_receive_time_code(void* raw,
                                           spw_time_code_t* code,
                                           spw_timeout_us_t timeout_us) {
    fake_driver_t* d = (fake_driver_t*)raw;
    (void)timeout_us;
    if (d->state != SPW_LINK_RUN) {
        return SPW_ERR_INVALID_STATE;
    }
    if (!d->time_code_ready) {
        return SPW_ERR_TIMEOUT;
    }
    *code = d->time_code;
    d->time_code_ready = false;
    ++d->statistics.rx_time_codes;
    return SPW_OK;
}

static spw_result_t fake_get_statistics(
    const void* raw,
    spw_statistics_t* out_statistics) {
    const fake_driver_t* d = (const fake_driver_t*)raw;
    *out_statistics = d->statistics;
    return SPW_OK;
}

static spw_result_t fake_clear_statistics(void* raw) {
    fake_driver_t* d = (fake_driver_t*)raw;
    memset(&d->statistics, 0, sizeof(d->statistics));
    return SPW_OK;
}

static spw_result_t fake_wait(void* raw,
                              spw_ready_events_t interests,
                              spw_timeout_us_t timeout_us,
                              spw_ready_events_t* out_ready) {
    fake_driver_t* d = (fake_driver_t*)raw;
    spw_ready_events_t ready = SPW_READY_NONE;
    (void)timeout_us;
    if (d->packet_ready) {
        ready |= SPW_READY_RX_PACKET;
    }
    if (d->time_code_ready) {
        ready |= SPW_READY_RX_TIME_CODE;
    }
    *out_ready = ready & interests;
    return *out_ready != SPW_READY_NONE ? SPW_OK : SPW_ERR_TIMEOUT;
}

static const spw_driver_ops_t FAKE_OPS = {
    sizeof(spw_driver_ops_t), SPW_DRIVER_OPS_VERSION,
    fake_start, fake_stop, fake_reset,
    fake_get_link_state, fake_get_capabilities,
    fake_send, fake_receive,
    fake_send_time_code, fake_receive_time_code,
    fake_get_statistics, fake_clear_statistics,
    fake_wait,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

static const spw_driver_ops_t FAKE_OPS_NO_WAIT = {
    sizeof(spw_driver_ops_t), SPW_DRIVER_OPS_VERSION,
    fake_start, fake_stop, fake_reset,
    fake_get_link_state, fake_get_capabilities,
    fake_send, fake_receive,
    fake_send_time_code, fake_receive_time_code,
    fake_get_statistics, fake_clear_statistics,
    NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

typedef union workspace_storage {
    long double align_long_double;
    void* align_pointer;
    uint64_t align_integer;
    unsigned char bytes[2048];
} workspace_storage_t;

static int run_driver(bool with_wait) {
    fake_driver_t driver;
    spw_driver_config_t driver_config;
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DRIVER);
    spw_port_workspace_requirements_t requirements;
    workspace_storage_t workspace;
    spw_port_t* port = NULL;
    spw_capabilities_t capabilities;
    const uint8_t tx_data[] = {0x10u, 0x20u, 0x30u};
    uint8_t rx_data[3] = {0u, 0u, 0u};
    uint8_t short_data[2] = {0u, 0u};
    spw_packet_t tx = {(uint8_t*)tx_data, sizeof(tx_data), sizeof(tx_data),
                       SPW_TERMINATOR_EEP};
    spw_packet_t short_rx = {short_data, 0u, sizeof(short_data),
                             SPW_TERMINATOR_EOP};
    spw_packet_t rx = {rx_data, 0u, sizeof(rx_data), SPW_TERMINATOR_EOP};
    spw_time_code_t tx_code = {17u, 0u};
    spw_time_code_t rx_code = {0u, 0u};
    spw_statistics_t statistics;
    spw_ready_events_t ready = SPW_READY_NONE;

    memset(&driver, 0, sizeof(driver));
    driver.state = SPW_LINK_ERROR_RESET;
    driver.advertise_readiness = with_wait;
    driver_config = (spw_driver_config_t)
        SPW_DRIVER_CONFIG_INITIALIZER(with_wait ? &FAKE_OPS : &FAKE_OPS_NO_WAIT,
                                      &driver);
    config.backend_config = &driver_config;
    config.backend_config_size = sizeof(driver_config);

    CHECK(spw_port_workspace_requirements(&config, &requirements) == SPW_OK);
    CHECK(requirements.size <= sizeof(workspace.bytes));
    CHECK(spw_port_open_in_place(&config,
                                 workspace.bytes,
                                 sizeof(workspace.bytes),
                                 &port) == SPW_OK);
    CHECK(port != NULL);
    CHECK(spw_port_start(port) == SPW_OK);
    CHECK(spw_port_get_capabilities(port, &capabilities) == SPW_OK);
    CHECK((capabilities.bits & SPW_CAP_TIME_CODE) != 0u);
    CHECK((capabilities.bits & SPW_CAP_STATISTICS) != 0u);
    CHECK(((capabilities.bits & SPW_CAP_READINESS) != 0u) == with_wait);

    CHECK(spw_port_send(port, &tx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    if (with_wait) {
        CHECK(spw_port_wait(port,
                            SPW_READY_RX_PACKET,
                            SPW_TIMEOUT_IMMEDIATE,
                            &ready) == SPW_OK);
        CHECK(ready == SPW_READY_RX_PACKET);
    } else {
        CHECK(spw_port_wait(port,
                            SPW_READY_RX_PACKET,
                            SPW_TIMEOUT_IMMEDIATE,
                            &ready) == SPW_ERR_UNSUPPORTED);
    }
    CHECK(spw_port_receive(port, &short_rx, SPW_TIMEOUT_IMMEDIATE) ==
          SPW_ERR_BUFFER_TOO_SMALL);
    CHECK(short_rx.length == sizeof(tx_data));
    CHECK(spw_port_receive(port, &rx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    CHECK(rx.length == sizeof(tx_data));
    CHECK(rx.terminator == SPW_TERMINATOR_EEP);
    CHECK(memcmp(rx.data, tx_data, sizeof(tx_data)) == 0);

    CHECK(spw_port_send_time_code(port,
                                  &tx_code,
                                  SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    if (with_wait) {
        CHECK(spw_port_wait(port,
                            SPW_READY_RX_TIME_CODE,
                            SPW_TIMEOUT_IMMEDIATE,
                            &ready) == SPW_OK);
        CHECK(ready == SPW_READY_RX_TIME_CODE);
    }
    CHECK(spw_port_receive_time_code(port,
                                     &rx_code,
                                     SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    CHECK(rx_code.time_count == tx_code.time_count);
    CHECK(rx_code.control_flags == tx_code.control_flags);

    CHECK(spw_port_get_statistics(port, &statistics) == SPW_OK);
    CHECK(statistics.tx_packets == 1u);
    CHECK(statistics.rx_packets == 1u);
    CHECK(statistics.tx_bytes == sizeof(tx_data));
    CHECK(statistics.rx_bytes == sizeof(tx_data));
    CHECK(statistics.tx_time_codes == 1u);
    CHECK(statistics.rx_time_codes == 1u);
    CHECK(statistics.eep_packets == 1u);
    CHECK(spw_port_clear_statistics(port) == SPW_OK);
    CHECK(spw_port_get_statistics(port, &statistics) == SPW_OK);
    CHECK(statistics.tx_packets == 0u);

    CHECK(spw_port_stop(port) == SPW_OK);
    CHECK(spw_port_send(port, &tx, SPW_TIMEOUT_IMMEDIATE) ==
          SPW_ERR_INVALID_STATE);
    CHECK(spw_port_reset(port) == SPW_OK);
    CHECK(spw_port_close(port) == SPW_OK);
    return 0;
}

int main(void) {
    CHECK(run_driver(true) == 0);
    CHECK(run_driver(false) == 0);
    return 0;
}
