// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static spw_result_t open_endpoint(uint64_t link_id,
                                  spw_simulator_endpoint_t endpoint,
                                  spw_port_t** out_port) {
    spw_simulator_config_t sim = SPW_SIMULATOR_CONFIG_INITIALIZER;
    sim.link_id = link_id;
    sim.endpoint = endpoint;

    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
    config.backend_config = &sim;
    config.backend_config_size = sizeof(sim);
    return spw_port_open(&config, out_port);
}

int main(void) {
    spw_port_t* a = NULL;
    spw_port_t* b = NULL;
    const uint64_t link_id = UINT64_C(0x7a65726f636f7079);

    if (open_endpoint(link_id, SPW_SIMULATOR_ENDPOINT_A, &a) != SPW_OK ||
        open_endpoint(link_id, SPW_SIMULATOR_ENDPOINT_B, &b) != SPW_OK ||
        a == NULL || b == NULL) {
        return 1;
    }
    if (spw_port_start(a) != SPW_OK || spw_port_start(b) != SPW_OK) {
        (void)spw_port_close(b);
        (void)spw_port_close(a);
        return 2;
    }

    spw_capabilities_t caps = {0};
    if (spw_port_get_capabilities(a, &caps) != SPW_OK ||
        (caps.bits & SPW_CAP_ZERO_COPY) == 0u) {
        (void)spw_port_close(b);
        (void)spw_port_close(a);
        return 3;
    }

    spw_buffer_t* tx = NULL;
    if (spw_port_acquire_tx_buffer(a, 5u, SPW_TIMEOUT_IMMEDIATE, &tx) != SPW_OK ||
        tx == NULL) {
        (void)spw_port_close(b);
        (void)spw_port_close(a);
        return 4;
    }

    spw_buffer_view_t tx_view = {0};
    if (spw_buffer_get_view(tx, &tx_view) != SPW_OK || tx_view.capacity < 5u) {
        (void)spw_port_release_tx_buffer(a, &tx);
        (void)spw_port_close(b);
        (void)spw_port_close(a);
        return 5;
    }

    const uint8_t payload[5] = {0x53u, 0x70u, 0x57u, 0x4bu, 0x21u};
    memcpy(tx_view.data, payload, sizeof(payload));
    if (spw_buffer_set_packet(tx, sizeof(payload), SPW_TERMINATOR_EEP) != SPW_OK ||
        spw_port_submit_tx_buffer(a, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK ||
        tx != NULL) {
        (void)spw_port_close(b);
        (void)spw_port_close(a);
        return 6;
    }

    spw_buffer_t* rx = NULL;
    if (spw_port_acquire_rx_buffer(b, SPW_TIMEOUT_IMMEDIATE, &rx) != SPW_OK ||
        rx == NULL) {
        (void)spw_port_close(b);
        (void)spw_port_close(a);
        return 7;
    }

    spw_buffer_view_t rx_view = {0};
    if (spw_buffer_get_view(rx, &rx_view) != SPW_OK ||
        rx_view.length != sizeof(payload) ||
        rx_view.terminator != SPW_TERMINATOR_EEP ||
        memcmp(rx_view.data, payload, sizeof(payload)) != 0) {
        (void)spw_port_release_rx_buffer(b, &rx);
        (void)spw_port_close(b);
        (void)spw_port_close(a);
        return 8;
    }

    if (spw_port_release_rx_buffer(b, &rx) != SPW_OK || rx != NULL) {
        (void)spw_port_close(b);
        (void)spw_port_close(a);
        return 9;
    }

    if (spw_port_reclaim_tx_buffer(a, SPW_TIMEOUT_IMMEDIATE, &tx) != SPW_OK ||
        tx == NULL ||
        spw_port_release_tx_buffer(a, &tx) != SPW_OK || tx != NULL) {
        (void)spw_port_close(b);
        (void)spw_port_close(a);
        return 10;
    }

    printf("zero-copy ownership contract completed\n");
    return (spw_port_close(b) == SPW_OK && spw_port_close(a) == SPW_OK) ? 0 : 11;
}
