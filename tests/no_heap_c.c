// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include <assert.h>
#include <stddef.h>
#include <stdalign.h>
#include <stdint.h>
#include <string.h>

int main(void) {
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
    spw_port_workspace_requirements_t requirements = {0u, 0u};
    spw_port_t* port = NULL;

    assert(spw_port_open(&config, &port) == SPW_ERR_UNSUPPORTED);
    assert(port == NULL);

    assert(spw_port_workspace_requirements(&config, &requirements) == SPW_OK);
    assert(requirements.size > 0u);
    assert(requirements.alignment > 0u);

    /*
     * The loopback backend deliberately keeps its bounded queues inside the
     * caller-owned workspace, so the advertised requirement is much larger
     * than one packet. Keep a fixed test arena comfortably above the current
     * contract while still failing if that contract grows unexpectedly.
     */
    alignas(max_align_t) static uint8_t workspace[64u * 1024u];
    assert(requirements.size <= sizeof(workspace));
    assert(((uintptr_t)workspace % requirements.alignment) == 0u);

    assert(spw_port_open_in_place(
               &config, workspace, requirements.size - 1u, &port) ==
           SPW_ERR_BUFFER_TOO_SMALL);
    assert(port == NULL);

    assert(spw_port_open_in_place(
               &config, workspace, sizeof(workspace), &port) == SPW_OK);
    assert(port != NULL);
    assert(spw_port_start(port) == SPW_OK);

    uint8_t tx_data[] = {0x43u, 0x2du, 0x4fu, 0x4eu, 0x4cu, 0x59u};
    spw_packet_t tx = {tx_data, sizeof(tx_data), sizeof(tx_data), SPW_TERMINATOR_EEP};
    assert(spw_port_send(port, &tx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);

    uint8_t rx_data[sizeof(tx_data)] = {0u};
    spw_packet_t rx = {rx_data, 0u, sizeof(rx_data), SPW_TERMINATOR_EOP};
    assert(spw_port_receive(port, &rx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(rx.length == sizeof(tx_data));
    assert(rx.terminator == SPW_TERMINATOR_EEP);
    assert(memcmp(tx_data, rx_data, sizeof(tx_data)) == 0);

    spw_time_code_t tx_time = {23u, 0u};
    spw_time_code_t rx_time = {0u, 0u};
    assert(spw_port_send_time_code(port, &tx_time, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(spw_port_receive_time_code(port, &rx_time, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(rx_time.time_count == tx_time.time_count);

    assert(spw_port_close(port) == SPW_OK);

    /* Closing an in-place port must make the same caller arena reusable. */
    port = NULL;
    assert(spw_port_open_in_place(
               &config, workspace, sizeof(workspace), &port) == SPW_OK);
    assert(spw_port_start(port) == SPW_OK);
    assert(spw_port_close(port) == SPW_OK);
    return 0;
}
