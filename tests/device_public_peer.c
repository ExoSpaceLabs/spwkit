// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L

#include <spwkit/spwkit.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TEST_TIMEOUT_US UINT64_C(5000000)
#define TEST_BIG_PACKET_SIZE 70000u

static void delay_ms(long ms) {
    struct timespec delay;
    delay.tv_sec = ms / 1000;
    delay.tv_nsec = (ms % 1000) * 1000000L;
    (void)nanosleep(&delay, NULL);
}

static void fill_pattern(uint8_t* data, size_t size, uint8_t seed) {
    size_t i;
    for (i = 0u; i < size; ++i) {
        data[i] = (uint8_t)(seed + (uint8_t)(i * 29u));
    }
}

static int wait_state(spw_port_t* port, spw_link_state_t expected) {
    unsigned i;
    for (i = 0u; i < 250u; ++i) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        spw_result_t result = spw_port_get_link_state(port, &state);
        if (result == SPW_OK && state == expected) {
            return 1;
        }
        delay_ms(20);
    }
    return 0;
}

static spw_port_t* open_started(const char* endpoint, uint32_t port_id) {
    spw_device_config_t device = SPW_DEVICE_CONFIG_INITIALIZER(port_id);
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);
    spw_port_t* port = NULL;
    size_t length = strlen(endpoint);
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

static int send_packet(spw_port_t* port,
                       uint8_t* data,
                       size_t size,
                       spw_terminator_t terminator) {
    spw_packet_t packet;
    packet.data = data;
    packet.length = size;
    packet.capacity = size;
    packet.terminator = terminator;
    return spw_port_send(port, &packet, TEST_TIMEOUT_US) == SPW_OK;
}

static int receive_packet(spw_port_t* port,
                          uint8_t* storage,
                          size_t capacity,
                          const uint8_t* expected,
                          size_t expected_size,
                          spw_terminator_t expected_terminator,
                          int verify_small_retry) {
    spw_packet_t packet;
    uint8_t small[16];
    if (verify_small_retry && expected_size > sizeof(small)) {
        packet.data = small;
        packet.length = 0u;
        packet.capacity = sizeof(small);
        packet.terminator = SPW_TERMINATOR_EOP;
        if (spw_port_receive(port, &packet, TEST_TIMEOUT_US) !=
                SPW_ERR_BUFFER_TOO_SMALL ||
            packet.length != expected_size ||
            packet.terminator != expected_terminator) {
            return 0;
        }
    }
    packet.data = storage;
    packet.length = 0u;
    packet.capacity = capacity;
    packet.terminator = SPW_TERMINATOR_EOP;
    if (spw_port_receive(port, &packet, TEST_TIMEOUT_US) != SPW_OK ||
        packet.length != expected_size ||
        packet.terminator != expected_terminator) {
        return 0;
    }
    return expected_size == 0u || memcmp(storage, expected, expected_size) == 0;
}

static int run_survivor(const char* endpoint) {
    spw_port_t* port = open_started(endpoint, 0u);
    static uint8_t big[TEST_BIG_PACKET_SIZE];
    static uint8_t rx[TEST_BIG_PACKET_SIZE];
    static uint8_t empty_storage[1];
    static const uint8_t reply[] = {0x42u, 0x2du, 0x3eu, 0x41u};
    static const uint8_t restart_reply[] = {0x52u, 0x45u, 0x53u, 0x54u};
    spw_packet_t no_packet;
    spw_time_code_t time_code;
    spw_statistics_t statistics;
    int ok = 0;

    if (port == NULL) {
        return 1;
    }
    no_packet.data = rx;
    no_packet.length = 0u;
    no_packet.capacity = sizeof(rx);
    no_packet.terminator = SPW_TERMINATOR_EOP;
    if (spw_port_receive(port, &no_packet, SPW_TIMEOUT_IMMEDIATE) != SPW_ERR_TIMEOUT) {
        goto done;
    }
    fill_pattern(big, sizeof(big), 0x31u);
    if (!wait_state(port, SPW_LINK_RUN) ||
        !send_packet(port, big, sizeof(big), SPW_TERMINATOR_EOP) ||
        !receive_packet(port,
                        rx,
                        sizeof(rx),
                        reply,
                        sizeof(reply),
                        SPW_TERMINATOR_EEP,
                        0) ||
        spw_port_receive_time_code(port, &time_code, TEST_TIMEOUT_US) != SPW_OK ||
        time_code.time_count != 17u || time_code.control_flags != 0u ||
        spw_port_get_statistics(port, &statistics) != SPW_OK ||
        statistics.tx_packets == 0u || statistics.rx_packets == 0u ||
        !wait_state(port, SPW_LINK_ERROR_WAIT) ||
        !wait_state(port, SPW_LINK_RUN) ||
        !send_packet(port, NULL, 0u, SPW_TERMINATOR_EEP) ||
        !receive_packet(port,
                        empty_storage,
                        sizeof(empty_storage),
                        restart_reply,
                        sizeof(restart_reply),
                        SPW_TERMINATOR_EOP,
                        0)) {
        goto done;
    }
    ok = 1;

done:
    (void)spw_port_close(port);
    return ok ? 0 : 1;
}

static int run_initial(const char* endpoint) {
    spw_port_t* port = open_started(endpoint, 1u);
    static uint8_t big[TEST_BIG_PACKET_SIZE];
    static uint8_t rx[TEST_BIG_PACKET_SIZE];
    static const uint8_t reply[] = {0x42u, 0x2du, 0x3eu, 0x41u};
    spw_time_code_t time_code = {17u, 0u};
    int ok = 0;

    if (port == NULL) {
        return 1;
    }
    fill_pattern(big, sizeof(big), 0x31u);
    if (!wait_state(port, SPW_LINK_RUN) ||
        !receive_packet(port,
                        rx,
                        sizeof(rx),
                        big,
                        sizeof(big),
                        SPW_TERMINATOR_EOP,
                        1) ||
        !send_packet(port, (uint8_t*)reply, sizeof(reply), SPW_TERMINATOR_EEP) ||
        spw_port_send_time_code(port, &time_code, TEST_TIMEOUT_US) != SPW_OK) {
        goto done;
    }
    ok = 1;

done:
    (void)spw_port_close(port);
    return ok ? 0 : 1;
}

static int run_restart(const char* endpoint) {
    spw_port_t* port = open_started(endpoint, 1u);
    static uint8_t storage[8];
    static const uint8_t reply[] = {0x52u, 0x45u, 0x53u, 0x54u};
    int ok = 0;
    if (port == NULL) {
        return 1;
    }
    if (!wait_state(port, SPW_LINK_RUN) ||
        !receive_packet(port,
                        storage,
                        sizeof(storage),
                        NULL,
                        0u,
                        SPW_TERMINATOR_EEP,
                        0) ||
        !send_packet(port, (uint8_t*)reply, sizeof(reply), SPW_TERMINATOR_EOP)) {
        goto done;
    }
    ok = 1;

done:
    (void)spw_port_close(port);
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s SOCKET survivor|initial|restart\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[2], "survivor") == 0) {
        return run_survivor(argv[1]);
    }
    if (strcmp(argv[2], "initial") == 0) {
        return run_initial(argv[1]);
    }
    if (strcmp(argv[2], "restart") == 0) {
        return run_restart(argv[1]);
    }
    return 2;
}
