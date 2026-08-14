// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
    spw_port_t* port = NULL;

    assert(spw_port_open(&config, &port) == SPW_OK);
    assert(port != NULL);

    spw_link_state_t state = 0xffu;
    assert(spw_port_get_link_state(port, &state) == SPW_OK);
    assert(state == SPW_LINK_ERROR_RESET);
    assert(spw_port_start(port) == SPW_OK);
    assert(spw_port_get_link_state(port, &state) == SPW_OK);
    assert(state == SPW_LINK_RUN);

    uint8_t tx_data[] = {0x53u, 0x50u, 0x57u};
    spw_packet_t tx = {tx_data, sizeof(tx_data), sizeof(tx_data), SPW_TERMINATOR_EOP};
    assert(spw_port_send(port, &tx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);

    uint8_t rx_data[sizeof(tx_data)] = {0u};
    spw_packet_t rx = {rx_data, 0u, sizeof(rx_data), SPW_TERMINATOR_EEP};
    assert(spw_port_receive(port, &rx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(rx.length == sizeof(tx_data));
    assert(rx.terminator == SPW_TERMINATOR_EOP);
    assert(memcmp(tx_data, rx_data, sizeof(tx_data)) == 0);

    spw_capabilities_t capabilities = {0};
    assert(spw_port_get_capabilities(port, &capabilities) == SPW_OK);
    assert((capabilities.bits & SPW_CAP_LINK_CONTROL) != 0u);

    assert(spw_port_close(port) == SPW_OK);

    spw_simulator_config_t simulator = SPW_SIMULATOR_CONFIG_INITIALIZER;
    simulator.link_id = 42u;
    simulator.endpoint = SPW_SIMULATOR_ENDPOINT_B;

    config = (spw_port_config_t)SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
    config.backend_config = &simulator;
    config.backend_config_size = sizeof(simulator);
    port = NULL;

    /* #4 replaces this expected result once the two-peer simulator exists. */
    assert(spw_port_open(&config, &port) == SPW_ERR_UNSUPPORTED);
    assert(port == NULL);

    simulator.endpoint = 9u;
    assert(spw_port_open(&config, &port) == SPW_ERR_INVALID_ARGUMENT);

    config.backend = 0xffffffffu;
    config.backend_config = NULL;
    config.backend_config_size = 0u;
    assert(spw_port_open(&config, &port) == SPW_ERR_UNSUPPORTED);

    return 0;
}
