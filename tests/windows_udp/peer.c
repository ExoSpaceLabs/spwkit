// SPDX-License-Identifier: Apache-2.0

#include <spwkit/port.h>
#include <spwkit/udp.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    PAYLOAD_SIZE = 4096,
    STATE_WAIT_MS = 8000,
    PEER_TIMEOUT_MS = 300
};

static void sleep_ms(unsigned milliseconds) {
    Sleep(milliseconds);
}

static int fail_result(const char* operation, spw_result_t result) {
    fprintf(stderr, "%s failed: %d\n", operation, (int)result);
    return EXIT_FAILURE;
}

static int wait_for_state(spw_port_t* port,
                          spw_link_state_t expected,
                          unsigned timeout_ms) {
    const unsigned attempts = timeout_ms / 10u + 1u;
    for (unsigned i = 0u; i < attempts; ++i) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        const spw_result_t result = spw_port_get_link_state(port, &state);
        if (result != SPW_OK) {
            fprintf(stderr, "get_link_state failed: %d\n", (int)result);
            return 0;
        }
        if (state == expected) {
            return 1;
        }
        sleep_ms(10u);
    }
    return 0;
}

static spw_port_t* open_peer(uint16_t local_port,
                             uint16_t remote_port,
                             uint32_t link_id) {
    spw_udp_config_t udp =
        SPW_UDP_CONFIG_INITIALIZER(local_port, remote_port, link_id);
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
    spw_port_t* port = NULL;

    udp.fragment_payload_size = 600u;
    udp.ack_timeout_ms = 50u;
    udp.max_retries = 5u;
    udp.keepalive_interval_ms = 50u;
    udp.peer_timeout_ms = PEER_TIMEOUT_MS;

    config.backend_config = &udp;
    config.backend_config_size = sizeof(udp);

    if (spw_port_open(&config, &port) != SPW_OK || port == NULL) {
        fprintf(stderr, "spw_port_open failed\n");
        return NULL;
    }
    if (spw_port_start(port) != SPW_OK) {
        fprintf(stderr, "spw_port_start failed\n");
        (void)spw_port_close(port);
        return NULL;
    }
    return port;
}

static uint8_t payload_byte(char sender, unsigned round, size_t index) {
    const unsigned seed = sender == 'A' ? 0x31u : 0xa7u;
    return (uint8_t)((seed + round * 19u + (unsigned)(index * 13u)) & 0xffu);
}

static void fill_payload(uint8_t* data, char sender, unsigned round) {
    for (size_t i = 0u; i < PAYLOAD_SIZE; ++i) {
        data[i] = payload_byte(sender, round, i);
    }
}

static int verify_payload(const uint8_t* data, char sender, unsigned round) {
    for (size_t i = 0u; i < PAYLOAD_SIZE; ++i) {
        if (data[i] != payload_byte(sender, round, i)) {
            fprintf(stderr,
                    "payload mismatch sender=%c round=%u index=%zu expected=%u got=%u\n",
                    sender, round, i,
                    (unsigned)payload_byte(sender, round, i),
                    (unsigned)data[i]);
            return 0;
        }
    }
    return 1;
}

static spw_time_code_t make_time_code(char sender, unsigned round) {
    spw_time_code_t code;
    code.time_count = (uint8_t)(10u + round + (sender == 'A' ? 0u : 20u));
    code.control_flags = (uint8_t)(sender == 'A' ? 1u : 2u);
    return code;
}

static int send_round(spw_port_t* port, char sender, unsigned round) {
    uint8_t data[PAYLOAD_SIZE];
    spw_packet_t packet;
    const spw_time_code_t code = make_time_code(sender, round);
    spw_result_t result;

    fill_payload(data, sender, round);
    packet.data = data;
    packet.length = PAYLOAD_SIZE;
    packet.capacity = PAYLOAD_SIZE;
    packet.terminator = sender == 'A' ? SPW_TERMINATOR_EEP
                                      : SPW_TERMINATOR_EOP;

    result = spw_port_send(port, &packet, 3000000u);
    if (result != SPW_OK) {
        return fail_result("spw_port_send", result);
    }
    result = spw_port_send_time_code(port, &code, 3000000u);
    if (result != SPW_OK) {
        return fail_result("spw_port_send_time_code", result);
    }
    return EXIT_SUCCESS;
}

static int receive_round(spw_port_t* port, char sender, unsigned round) {
    uint8_t data[PAYLOAD_SIZE];
    spw_packet_t packet;
    spw_time_code_t code;
    const spw_time_code_t expected_code = make_time_code(sender, round);
    const spw_terminator_t expected_terminator =
        sender == 'A' ? SPW_TERMINATOR_EEP : SPW_TERMINATOR_EOP;
    spw_result_t result;

    memset(data, 0, sizeof(data));
    memset(&code, 0, sizeof(code));
    packet.data = data;
    packet.length = 0u;
    packet.capacity = sizeof(data);
    packet.terminator = SPW_TERMINATOR_EOP;

    result = spw_port_receive(port, &packet, 3000000u);
    if (result != SPW_OK) {
        return fail_result("spw_port_receive", result);
    }
    if (packet.length != PAYLOAD_SIZE ||
        packet.terminator != expected_terminator ||
        !verify_payload(data, sender, round)) {
        fprintf(stderr, "packet validation failed sender=%c round=%u\n",
                sender, round);
        return EXIT_FAILURE;
    }

    result = spw_port_receive_time_code(port, &code, 3000000u);
    if (result != SPW_OK) {
        return fail_result("spw_port_receive_time_code", result);
    }
    if (code.time_count != expected_code.time_count ||
        code.control_flags != expected_code.control_flags) {
        fprintf(stderr, "time-code validation failed sender=%c round=%u\n",
                sender, round);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int run_a(uint16_t base_port) {
    const uint32_t link_id = UINT32_C(0x57535057);
    spw_port_t* port = open_peer(base_port, (uint16_t)(base_port + 1u), link_id);
    int result = EXIT_FAILURE;

    if (port == NULL) {
        return EXIT_FAILURE;
    }
    if (!wait_for_state(port, SPW_LINK_RUN, STATE_WAIT_MS)) {
        fprintf(stderr, "A did not reach RUN for round 1\n");
        goto done;
    }
    if (send_round(port, 'A', 1u) != EXIT_SUCCESS ||
        receive_round(port, 'B', 1u) != EXIT_SUCCESS) {
        goto done;
    }
    puts("A round 1 complete");

    if (!wait_for_state(port, SPW_LINK_ERROR_WAIT, STATE_WAIT_MS)) {
        fprintf(stderr, "A did not observe peer loss\n");
        goto done;
    }
    puts("A observed peer loss");

    if (!wait_for_state(port, SPW_LINK_RUN, STATE_WAIT_MS)) {
        fprintf(stderr, "A did not recover RUN after peer restart\n");
        goto done;
    }
    if (send_round(port, 'A', 2u) != EXIT_SUCCESS ||
        receive_round(port, 'B', 2u) != EXIT_SUCCESS) {
        goto done;
    }
    puts("A restart recovery complete");
    result = EXIT_SUCCESS;

done:
    if (spw_port_close(port) != SPW_OK) {
        fprintf(stderr, "A close failed\n");
        result = EXIT_FAILURE;
    }
    return result;
}

static int run_b(uint16_t base_port, unsigned round) {
    const uint32_t link_id = UINT32_C(0x57535057);
    spw_port_t* port = open_peer((uint16_t)(base_port + 1u), base_port, link_id);
    int result = EXIT_FAILURE;

    if (port == NULL) {
        return EXIT_FAILURE;
    }
    if (!wait_for_state(port, SPW_LINK_RUN, STATE_WAIT_MS)) {
        fprintf(stderr, "B did not reach RUN for round %u\n", round);
        goto done;
    }
    if (receive_round(port, 'A', round) != EXIT_SUCCESS ||
        send_round(port, 'B', round) != EXIT_SUCCESS) {
        goto done;
    }

    /* Give the peer time to consume and ACK the final logical message. */
    for (unsigned i = 0u; i < 20u; ++i) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(port, &state) != SPW_OK) {
            goto done;
        }
        sleep_ms(10u);
    }

    printf("B round %u complete\n", round);
    result = EXIT_SUCCESS;

done:
    if (spw_port_close(port) != SPW_OK) {
        fprintf(stderr, "B close failed\n");
        result = EXIT_FAILURE;
    }
    return result;
}

static int parse_port(const char* text, uint16_t* out_port) {
    char* end = NULL;
    const unsigned long value = strtoul(text, &end, 10);
    if (text == NULL || text[0] == '\0' || end == NULL || *end != '\0' ||
        value < 1024ul || value > 65534ul) {
        return 0;
    }
    *out_port = (uint16_t)value;
    return 1;
}

int main(int argc, char** argv) {
    uint16_t base_port = 0u;
    if (argc < 3 || argv[1][1] != '\0' ||
        (argv[1][0] != 'A' && argv[1][0] != 'B') ||
        !parse_port(argv[2], &base_port)) {
        fprintf(stderr, "usage: %s A BASE_PORT | B BASE_PORT ROUND\n", argv[0]);
        return EXIT_FAILURE;
    }

    if (argv[1][0] == 'A') {
        if (argc != 3) {
            return EXIT_FAILURE;
        }
        return run_a(base_port);
    }

    if (argc != 4) {
        return EXIT_FAILURE;
    }
    {
        char* end = NULL;
        const unsigned long round = strtoul(argv[3], &end, 10);
        if (end == NULL || *end != '\0' || (round != 1ul && round != 2ul)) {
            return EXIT_FAILURE;
        }
        return run_b(base_port, (unsigned)round);
    }
}
