// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

static spw_port_config_t simulator_port_config(spw_simulator_config_t* simulator) {
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
    config.backend_config = simulator;
    config.backend_config_size = sizeof(*simulator);
    return config;
}

static void expect_packet(spw_port_t* port,
                          const uint8_t* expected,
                          size_t expected_size,
                          spw_terminator_t terminator) {
    uint8_t storage[32] = {0u};
    spw_packet_t packet = {storage, 0u, sizeof(storage), SPW_TERMINATOR_EOP};
    assert(expected_size <= sizeof(storage));
    assert(spw_port_receive(port, &packet, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(packet.length == expected_size);
    assert(packet.terminator == terminator);
    assert(memcmp(storage, expected, expected_size) == 0);
}

int main(void) {
    spw_simulator_config_t a_config = SPW_SIMULATOR_CONFIG_INITIALIZER;
    spw_simulator_config_t b_config = SPW_SIMULATOR_CONFIG_INITIALIZER;
    a_config.link_id = 0x434f5245u;
    b_config.link_id = a_config.link_id;
    a_config.endpoint = SPW_SIMULATOR_ENDPOINT_A;
    b_config.endpoint = SPW_SIMULATOR_ENDPOINT_B;

    spw_port_config_t a_port_config = simulator_port_config(&a_config);
    spw_port_config_t b_port_config = simulator_port_config(&b_config);
    spw_port_t* a = NULL;
    spw_port_t* b = NULL;

    assert(spw_port_open(&a_port_config, &a) == SPW_OK);
    assert(spw_port_open(&b_port_config, &b) == SPW_OK);
    assert(spw_port_start(a) == SPW_OK);
    assert(spw_port_start(b) == SPW_OK);

    spw_link_state_t state = SPW_LINK_ERROR_RESET;
    assert(spw_port_get_link_state(a, &state) == SPW_OK);
    assert(state == SPW_LINK_RUN);
    assert(spw_port_get_link_state(b, &state) == SPW_OK);
    assert(state == SPW_LINK_RUN);

    uint8_t a_to_b[] = {0x41u, 0x2du, 0x3eu, 0x42u};
    spw_packet_t first = {a_to_b, sizeof(a_to_b), sizeof(a_to_b), SPW_TERMINATOR_EOP};
    assert(spw_port_send(a, &first, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    expect_packet(b, a_to_b, sizeof(a_to_b), SPW_TERMINATOR_EOP);

    uint8_t b_to_a[] = {0x42u, 0x2du, 0x3eu, 0x41u};
    spw_packet_t second = {b_to_a, sizeof(b_to_a), sizeof(b_to_a), SPW_TERMINATOR_EEP};
    assert(spw_port_send(b, &second, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    expect_packet(a, b_to_a, sizeof(b_to_a), SPW_TERMINATOR_EEP);

    spw_time_code_t tx_time = {37u, 0u};
    spw_time_code_t rx_time = {0u, 0u};
    assert(spw_port_send_time_code(a, &tx_time, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(spw_port_receive_time_code(b, &rx_time, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(rx_time.time_count == tx_time.time_count);
    assert(rx_time.control_flags == tx_time.control_flags);

    assert(spw_port_stop(a) == SPW_OK);
    assert(spw_port_stop(b) == SPW_OK);
    assert(spw_port_close(a) == SPW_OK);
    assert(spw_port_close(b) == SPW_OK);
    return 0;
}
