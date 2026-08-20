// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L

#include <spwkit/spwkit.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TEST_TIMEOUT_US UINT64_C(3000000)

static void sleep_ms(long milliseconds) {
    struct timespec delay;
    delay.tv_sec = milliseconds / 1000;
    delay.tv_nsec = (milliseconds % 1000) * 1000000L;
    (void)nanosleep(&delay, NULL);
}

static int wait_run(spw_port_t* port) {
    unsigned i;
    for (i = 0u; i < 200u; ++i) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(port, &state) == SPW_OK &&
            state == SPW_LINK_RUN) {
            return 1;
        }
        sleep_ms(10);
    }
    return 0;
}

static int receive_packet(spw_port_t* port,
                          const uint8_t* expected,
                          size_t expected_size,
                          spw_terminator_t expected_terminator) {
    uint8_t storage[32];
    spw_packet_t packet = {
        storage,
        0u,
        sizeof(storage),
        SPW_TERMINATOR_EOP};
    spw_result_t result = spw_port_receive(port, &packet, TEST_TIMEOUT_US);
    if (result != SPW_OK || packet.length != expected_size ||
        packet.terminator != expected_terminator) {
        return 0;
    }
    return expected_size == 0u || memcmp(packet.data, expected, expected_size) == 0;
}

static int send_packet(spw_port_t* port,
                       uint8_t* data,
                       size_t size,
                       spw_terminator_t terminator) {
    spw_packet_t packet = {data, size, size, terminator};
    return spw_port_send(port, &packet, TEST_TIMEOUT_US) == SPW_OK;
}

int main(int argc, char** argv) {
    static const uint8_t hello[] = {'h', 'e', 'l', 'l', 'o'};
    static uint8_t world[] = {'w', 'o', 'r', 'l', 'd'};
    spw_device_config_t device = SPW_DEVICE_CONFIG_INITIALIZER(1u);
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);
    spw_port_t* port = NULL;
    spw_time_code_t time_code;
    size_t endpoint_size;
    int exit_code = 1;

    if (argc != 2) {
        fprintf(stderr, "usage: %s VSPD_SOCKET\n", argv[0]);
        return 2;
    }
    endpoint_size = strlen(argv[1]);
    if (endpoint_size >= sizeof(device.endpoint)) {
        return 2;
    }
    memcpy(device.endpoint, argv[1], endpoint_size + 1u);
    config.backend_config = &device;
    config.backend_config_size = sizeof(device);

    if (spw_port_open(&config, &port) != SPW_OK || port == NULL ||
        spw_port_start(port) != SPW_OK || !wait_run(port)) {
        goto done;
    }

    if (!receive_packet(port, hello, sizeof(hello), SPW_TERMINATOR_EOP)) {
        goto done;
    }
    if (spw_port_receive_time_code(port, &time_code, TEST_TIMEOUT_US) != SPW_OK ||
        time_code.time_count != 7u || time_code.control_flags != 0u) {
        goto done;
    }
    if (!receive_packet(port, NULL, 0u, SPW_TERMINATOR_EEP)) {
        goto done;
    }

    sleep_ms(150);
    if (!send_packet(port, world, sizeof(world), SPW_TERMINATOR_EEP)) {
        goto done;
    }
    sleep_ms(300);
    if (!send_packet(port, world, 0u, SPW_TERMINATOR_EOP)) {
        goto done;
    }
    sleep_ms(300);
    if (!send_packet(port, world, sizeof(world), SPW_TERMINATOR_EOP)) {
        goto done;
    }

    exit_code = 0;

done:
    if (port != NULL) {
        (void)spw_port_close(port);
    }
    return exit_code;
}
