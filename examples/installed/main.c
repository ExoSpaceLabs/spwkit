// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include <stdint.h>

int main(void) {
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
    spw_port_t* port = 0;
    if (spw_port_open(&config, &port) != SPW_OK || port == 0) {
        return 1;
    }
    if (spw_port_start(port) != SPW_OK) {
        (void)spw_port_close(port);
        return 2;
    }

    uint8_t value = 0x5au;
    spw_packet_t tx = {&value, 1u, 1u, SPW_TERMINATOR_EOP};
    if (spw_port_send(port, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK) {
        (void)spw_port_close(port);
        return 3;
    }

    uint8_t received = 0u;
    spw_packet_t rx = {&received, 0u, 1u, SPW_TERMINATOR_EEP};
    if (spw_port_receive(port, &rx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK ||
        received != value || rx.length != 1u ||
        rx.terminator != SPW_TERMINATOR_EOP) {
        (void)spw_port_close(port);
        return 4;
    }

    return spw_port_close(port) == SPW_OK ? 0 : 5;
}
