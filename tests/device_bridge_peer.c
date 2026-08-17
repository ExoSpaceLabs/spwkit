// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L

#include <spwkit/device.h>
#include <spwkit/port.h>
#include <spwkit/types.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const spw_timeout_us_t IO_TIMEOUT_US = 5000000u;
static const unsigned STATE_TIMEOUT_MS = 15000u;
static const size_t PAYLOAD_SIZE = 8192u;

static void sleep_ms(unsigned milliseconds) {
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / 1000u);
    delay.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    (void)nanosleep(&delay, NULL);
}

static int wait_for_state(spw_port_t* port, spw_link_state_t expected) {
    const unsigned attempts = STATE_TIMEOUT_MS / 10u + 1u;
    unsigned i;
    for (i = 0u; i < attempts; ++i) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(port, &state) != SPW_OK) {
            return 0;
        }
        if (state == expected) {
            return 1;
        }
        sleep_ms(10u);
    }
    return 0;
}

static uint8_t pattern_byte(char id, unsigned round, size_t index) {
    const unsigned base = id == 'A' ? 0x31u : 0x97u;
    return (uint8_t)((base + round * 17u + (unsigned)(index * 13u)) & 0xffu);
}

static void fill_payload(uint8_t* payload, char id, unsigned round) {
    size_t i;
    for (i = 0u; i < PAYLOAD_SIZE; ++i) {
        payload[i] = pattern_byte(id, round, i);
    }
}

static int verify_payload(const uint8_t* payload, char id, unsigned round) {
    size_t i;
    for (i = 0u; i < PAYLOAD_SIZE; ++i) {
        if (payload[i] != pattern_byte(id, round, i)) {
            return 0;
        }
    }
    return 1;
}

static int exchange_round(spw_port_t* port, unsigned round) {
    uint8_t tx_payload[8192];
    uint8_t rx_payload[8192];
    spw_packet_t tx;
    spw_packet_t rx;
    spw_time_code_t tx_time;
    spw_time_code_t rx_time;
    spw_result_t result;

    fill_payload(tx_payload, 'A', round);
    memset(rx_payload, 0, sizeof(rx_payload));
    tx.data = tx_payload;
    tx.length = PAYLOAD_SIZE;
    tx.capacity = PAYLOAD_SIZE;
    tx.terminator = SPW_TERMINATOR_EOP;
    result = spw_port_send(port, &tx, IO_TIMEOUT_US);
    if (result != SPW_OK) {
        fprintf(stderr, "device round %u send failed: %d\n", round, (int)result);
        return 0;
    }

    rx.data = rx_payload;
    rx.length = 0u;
    rx.capacity = PAYLOAD_SIZE;
    rx.terminator = SPW_TERMINATOR_EOP;
    result = spw_port_receive(port, &rx, IO_TIMEOUT_US);
    if (result != SPW_OK || rx.length != PAYLOAD_SIZE ||
        rx.terminator != SPW_TERMINATOR_EEP ||
        !verify_payload(rx_payload, 'B', round)) {
        fprintf(stderr, "device round %u receive mismatch result=%d\n", round, (int)result);
        return 0;
    }

    tx_time.time_count = (uint8_t)((round * 10u + 1u) & 0x3fu);
    tx_time.control_flags = 0u;
    if (spw_port_send_time_code(port, &tx_time, IO_TIMEOUT_US) != SPW_OK) {
        return 0;
    }
    if (spw_port_receive_time_code(port, &rx_time, IO_TIMEOUT_US) != SPW_OK ||
        rx_time.time_count != (uint8_t)((round * 10u + 2u) & 0x3fu) ||
        rx_time.control_flags != 0u) {
        return 0;
    }
    printf("ROUND %u OK device\n", round);
    return 1;
}

int main(int argc, char** argv) {
    spw_device_config_t device;
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);
    spw_port_t* port = NULL;
    unsigned long parsed_port;
    char* end = NULL;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc != 3) {
        fprintf(stderr, "usage: %s SOCKET PORT\n", argv[0]);
        return 2;
    }
    parsed_port = strtoul(argv[2], &end, 10);
    if (end == argv[2] || *end != '\0' || parsed_port > UINT32_MAX) {
        return 2;
    }
    device = (spw_device_config_t)SPW_DEVICE_CONFIG_INITIALIZER((uint32_t)parsed_port);
    if (snprintf(device.endpoint, sizeof(device.endpoint), "%s", argv[1]) >=
        (int)sizeof(device.endpoint)) {
        return 2;
    }
    config.backend_config = &device;
    config.backend_config_size = sizeof(device);
    if (spw_port_open(&config, &port) != SPW_OK || port == NULL ||
        spw_port_start(port) != SPW_OK) {
        return 1;
    }
    if (!wait_for_state(port, SPW_LINK_RUN) || !exchange_round(port, 1u)) {
        (void)spw_port_close(port);
        return 1;
    }
    if (!wait_for_state(port, SPW_LINK_ERROR_WAIT)) {
        fprintf(stderr, "device did not observe remote peer loss\n");
        (void)spw_port_close(port);
        return 1;
    }
    printf("PEER_LOST\n");
    if (!wait_for_state(port, SPW_LINK_RUN)) {
        fprintf(stderr, "device did not recover remote peer\n");
        (void)spw_port_close(port);
        return 1;
    }
    printf("PEER_RECOVERED\n");
    if (!exchange_round(port, 2u)) {
        (void)spw_port_close(port);
        return 1;
    }
    (void)spw_port_close(port);
    printf("PASS device survivor\n");
    return 0;
}
