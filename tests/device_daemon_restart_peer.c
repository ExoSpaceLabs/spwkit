// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L

#include <spwkit/spwkit.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define TEST_TIMEOUT_US UINT64_C(2000000)

static void delay_ms(long ms) {
    struct timespec delay;
    delay.tv_sec = ms / 1000;
    delay.tv_nsec = (ms % 1000) * 1000000L;
    (void)nanosleep(&delay, NULL);
}

static int write_marker(const char* path) {
    FILE* file = fopen(path, "w");
    if (file == NULL) {
        return 0;
    }
    if (fputs("ready\n", file) < 0) {
        (void)fclose(file);
        return 0;
    }
    return fclose(file) == 0;
}

static spw_port_t* open_started(const char* endpoint, uint32_t port_id) {
    spw_device_config_t device = SPW_DEVICE_CONFIG_INITIALIZER(port_id);
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);
    spw_port_t* port = NULL;
    const size_t length = strlen(endpoint);

    if (length >= sizeof(device.endpoint)) {
        return NULL;
    }
    memcpy(device.endpoint, endpoint, length + 1u);
    config.backend_config = &device;
    config.backend_config_size = sizeof(device);

    if (spw_port_open(&config, &port) != SPW_OK || port == NULL) {
        return NULL;
    }
    if (spw_port_start(port) != SPW_OK) {
        (void)spw_port_close(port);
        return NULL;
    }
    return port;
}

static int wait_running_pair(spw_port_t* a, spw_port_t* b) {
    unsigned attempt;
    for (attempt = 0u; attempt < 500u; ++attempt) {
        spw_link_state_t a_state = SPW_LINK_ERROR_RESET;
        spw_link_state_t b_state = SPW_LINK_ERROR_RESET;
        const spw_result_t a_result = spw_port_get_link_state(a, &a_state);
        const spw_result_t b_result = spw_port_get_link_state(b, &b_state);
        if (a_result == SPW_OK && b_result == SPW_OK &&
            a_state == SPW_LINK_RUN && b_state == SPW_LINK_RUN) {
            return 1;
        }
        delay_ms(10);
    }
    return 0;
}

static int wait_disconnected_pair(spw_port_t* a, spw_port_t* b) {
    int a_lost = 0;
    int b_lost = 0;
    unsigned attempt;

    for (attempt = 0u; attempt < 500u; ++attempt) {
        spw_link_state_t a_state = SPW_LINK_ERROR_RESET;
        spw_link_state_t b_state = SPW_LINK_ERROR_RESET;
        const spw_result_t a_result = spw_port_get_link_state(a, &a_state);
        const spw_result_t b_result = spw_port_get_link_state(b, &b_state);

        if (a_result == SPW_ERR_LINK_UNAVAILABLE ||
            a_state == SPW_LINK_ERROR_WAIT) {
            a_lost = 1;
        }
        if (b_result == SPW_ERR_LINK_UNAVAILABLE ||
            b_state == SPW_LINK_ERROR_WAIT) {
            b_lost = 1;
        }
        if (a_lost && b_lost) {
            return 1;
        }
        delay_ms(10);
    }
    return 0;
}

static int round_trip(spw_port_t* a, spw_port_t* b, uint8_t seed) {
    uint8_t tx[32];
    uint8_t rx[32];
    spw_packet_t outgoing;
    spw_packet_t incoming;
    size_t i;

    for (i = 0u; i < sizeof(tx); ++i) {
        tx[i] = (uint8_t)(seed + (uint8_t)(i * 7u));
    }

    outgoing.data = tx;
    outgoing.length = sizeof(tx);
    outgoing.capacity = sizeof(tx);
    outgoing.terminator = SPW_TERMINATOR_EEP;
    if (spw_port_send(a, &outgoing, TEST_TIMEOUT_US) != SPW_OK) {
        return 0;
    }

    incoming.data = rx;
    incoming.length = 0u;
    incoming.capacity = sizeof(rx);
    incoming.terminator = SPW_TERMINATOR_EOP;
    if (spw_port_receive(b, &incoming, TEST_TIMEOUT_US) != SPW_OK ||
        incoming.length != sizeof(tx) ||
        incoming.terminator != SPW_TERMINATOR_EEP ||
        memcmp(tx, rx, sizeof(tx)) != 0) {
        return 0;
    }
    return 1;
}

int main(int argc, char** argv) {
    spw_port_t* a = NULL;
    spw_port_t* b = NULL;
    int ok = 0;

    if (argc != 4) {
        fprintf(stderr, "usage: %s SOCKET READY_MARKER LOST_MARKER\n", argv[0]);
        return 2;
    }

    a = open_started(argv[1], 0u);
    b = open_started(argv[1], 1u);
    if (a == NULL || b == NULL ||
        !wait_running_pair(a, b) ||
        !round_trip(a, b, 0x21u) ||
        !write_marker(argv[2]) ||
        !wait_disconnected_pair(a, b) ||
        !write_marker(argv[3]) ||
        !wait_running_pair(a, b) ||
        !round_trip(b, a, 0x91u)) {
        goto done;
    }

    ok = 1;

done:
    if (b != NULL) {
        (void)spw_port_close(b);
    }
    if (a != NULL) {
        (void)spw_port_close(a);
    }
    return ok ? 0 : 1;
}
