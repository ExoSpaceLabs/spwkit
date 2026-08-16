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

    alignas(max_align_t) uint8_t workspace[4096];
    assert(requirements.size <= sizeof(workspace));
    assert(requirements.alignment <= alignof(max_align_t));

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

    assert(spw_port_close(port) == SPW_OK);
    return 0;
}
