// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L

#include <spwkit/spwkit.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define EXAMPLE_TIMEOUT_US UINT64_C(3000000)

static void delay_ms(long ms) {
    struct timespec delay = {ms / 1000, (ms % 1000) * 1000000L};
    (void)nanosleep(&delay, NULL);
}

static int wait_run(spw_port_t* port) {
    unsigned i;
    for (i = 0u; i < 150u; ++i) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(port, &state) == SPW_OK &&
            state == SPW_LINK_RUN) {
            return 1;
        }
        delay_ms(20);
    }
    return 0;
}

static int send_bytes(spw_port_t* port,
                      uint8_t* data,
                      size_t length,
                      spw_terminator_t terminator) {
    spw_packet_t packet = {data, length, length, terminator};
    return spw_port_send(port, &packet, EXAMPLE_TIMEOUT_US) == SPW_OK;
}

static int receive_bytes(spw_port_t* port,
                         const uint8_t* expected,
                         size_t expected_length,
                         spw_terminator_t terminator) {
    uint8_t storage[32];
    spw_packet_t packet = {storage, 0u, sizeof(storage), SPW_TERMINATOR_EOP};
    return spw_port_receive(port, &packet, EXAMPLE_TIMEOUT_US) == SPW_OK &&
           packet.length == expected_length && packet.terminator == terminator &&
           memcmp(packet.data, expected, expected_length) == 0;
}

int main(int argc, char** argv) {
    static uint8_t hello[] = {'h', 'e', 'l', 'l', 'o'};
    static uint8_t world[] = {'w', 'o', 'r', 'l', 'd'};
    spw_device_config_t device;
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);
    spw_port_t* port = NULL;
    unsigned long port_id;
    size_t endpoint_length;
    int result = 1;

    if (argc != 3) {
        fprintf(stderr, "usage: %s SOCKET 0|1\n", argv[0]);
        return 2;
    }
    port_id = strtoul(argv[2], NULL, 10);
    if (port_id > 1u) {
        return 2;
    }
    device = (spw_device_config_t)SPW_DEVICE_CONFIG_INITIALIZER((uint32_t)port_id);
    endpoint_length = strlen(argv[1]);
    if (endpoint_length >= sizeof(device.endpoint)) {
        return 2;
    }
    memcpy(device.endpoint, argv[1], endpoint_length + 1u);
    config.backend_config = &device;
    config.backend_config_size = sizeof(device);

    if (spw_port_open(&config, &port) != SPW_OK || port == NULL ||
        spw_port_start(port) != SPW_OK || !wait_run(port)) {
        goto done;
    }

    if (port_id == 0u) {
        spw_time_code_t time_code = {7u, 0u};
        if (!send_bytes(port, hello, sizeof(hello), SPW_TERMINATOR_EOP) ||
            spw_port_send_time_code(port, &time_code, EXAMPLE_TIMEOUT_US) != SPW_OK ||
            !receive_bytes(port, world, sizeof(world), SPW_TERMINATOR_EEP)) {
            goto done;
        }
    } else {
        spw_time_code_t time_code;
        if (!receive_bytes(port, hello, sizeof(hello), SPW_TERMINATOR_EOP) ||
            spw_port_receive_time_code(port, &time_code, EXAMPLE_TIMEOUT_US) != SPW_OK ||
            time_code.time_count != 7u || time_code.control_flags != 0u ||
            !send_bytes(port, world, sizeof(world), SPW_TERMINATOR_EEP)) {
            goto done;
        }
    }

    result = 0;

done:
    if (port != NULL) {
        (void)spw_port_close(port);
    }
    return result;
}
