// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include <stdint.h>

#ifndef SPWKIT_INSTALLED_EXPECT_UDP_RUNTIME
#error "installed consumer must receive SpWKit UDP runtime metadata"
#endif

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
    if (spw_port_close(port) != SPW_OK) {
        return 5;
    }

    /*
     * The installed package exports whether this particular library contains
     * the hosted UDP runtime. Public UDP types remain available either way.
     * Workspace discovery is enough to verify backend selection without
     * requiring a live network peer or heap-backed open.
     */
    spw_udp_config_t udp = SPW_UDP_CONFIG_INITIALIZER(0u, 42001u, 42u);
    config = (spw_port_config_t)SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
    config.backend_config = &udp;
    config.backend_config_size = sizeof(udp);

    spw_port_workspace_requirements_t requirements = {0u, 0u};
    const spw_result_t udp_result =
        spw_port_workspace_requirements(&config, &requirements);

#if SPWKIT_INSTALLED_EXPECT_UDP_RUNTIME
    if (udp_result != SPW_OK || requirements.size == 0u ||
        requirements.alignment == 0u) {
        return 6;
    }
#else
    if (udp_result != SPW_ERR_UNSUPPORTED || requirements.size != 0u ||
        requirements.alignment != 0u) {
        return 7;
    }
#endif

    return 0;
}
