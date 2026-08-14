// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int require_ok(const char* operation, spw_result_t result) {
    if (result == SPW_OK) {
        return 1;
    }
    fprintf(stderr, "%s failed: %d\n", operation, (int)result);
    return 0;
}

int main(void) {
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
    spw_port_t* port = NULL;

    if (!require_ok("open", spw_port_open(&config, &port)) || port == NULL) {
        return 1;
    }
    if (!require_ok("start", spw_port_start(port))) {
        (void)spw_port_close(port);
        return 1;
    }

    spw_capabilities_t caps = {0};
    if (!require_ok("capabilities", spw_port_get_capabilities(port, &caps))) {
        (void)spw_port_close(port);
        return 1;
    }
    printf("max packet: %zu bytes, queue depth: %zu\n",
           caps.max_packet_size, caps.rx_queue_depth);

    uint8_t payload[] = {0x53u, 0x70u, 0x57u, 0x4bu, 0x69u, 0x74u};
    spw_packet_t tx = {
        payload,
        sizeof(payload),
        sizeof(payload),
        SPW_TERMINATOR_EEP,
    };
    if (!require_ok("send", spw_port_send(port, &tx, SPW_TIMEOUT_IMMEDIATE))) {
        (void)spw_port_close(port);
        return 1;
    }

    uint8_t received[sizeof(payload)] = {0};
    spw_packet_t rx = {
        received,
        0u,
        sizeof(received),
        SPW_TERMINATOR_EOP,
    };
    if (!require_ok("receive", spw_port_receive(port, &rx, SPW_TIMEOUT_IMMEDIATE))) {
        (void)spw_port_close(port);
        return 1;
    }

    if (rx.length != sizeof(payload) ||
        rx.terminator != SPW_TERMINATOR_EEP ||
        memcmp(payload, received, sizeof(payload)) != 0) {
        fprintf(stderr, "packet verification failed\n");
        (void)spw_port_close(port);
        return 1;
    }

    if ((caps.bits & SPW_CAP_TIME_CODE) != 0u) {
        spw_time_code_t tx_time = {63u, 0u};
        spw_time_code_t rx_time = {0u, 0u};
        if (!require_ok("send time-code",
                        spw_port_send_time_code(port, &tx_time, SPW_TIMEOUT_IMMEDIATE)) ||
            !require_ok("receive time-code",
                        spw_port_receive_time_code(port, &rx_time, SPW_TIMEOUT_IMMEDIATE)) ||
            rx_time.time_count != tx_time.time_count) {
            (void)spw_port_close(port);
            return 1;
        }
    }

    if (!require_ok("close", spw_port_close(port))) {
        return 1;
    }
    return 0;
}
