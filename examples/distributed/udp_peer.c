// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L

#include <spwkit/port.h>
#include <spwkit/types.h>
#include <spwkit/udp.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum scenario {
    SCENARIO_SINGLE,
    SCENARIO_INITIAL,
    SCENARIO_SURVIVOR,
    SCENARIO_RESTART,
};

struct options {
    char local_address[SPW_UDP_ADDRESS_MAX];
    char remote_address[SPW_UDP_ADDRESS_MAX];
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t link_id;
    char id;
    enum scenario scenario;
};

static const spw_timeout_us_t IO_TIMEOUT_US = 5000000u;
static const unsigned STATE_TIMEOUT_MS = 15000u;
static const size_t PAYLOAD_SIZE = 8192u;

static void usage(const char* program) {
    fprintf(stderr,
            "usage: %s --id A|B --local-port PORT --remote-port PORT [options]\n"
            "\n"
            "options:\n"
            "  --local-address IPv4    default 127.0.0.1\n"
            "  --remote-address IPv4   default 127.0.0.1\n"
            "  --link-id ID            default 42\n"
            "  --scenario NAME         single|initial|survivor|restart\n",
            program);
}

static int parse_u32(const char* text, uint32_t* value) {
    char* end = NULL;
    const unsigned long parsed = strtoul(text, &end, 0);
    if (text[0] == '\0' || end == NULL || *end != '\0' || parsed > UINT32_MAX) {
        return 0;
    }
    *value = (uint32_t)parsed;
    return 1;
}

static int parse_port(const char* text, uint16_t* value) {
    uint32_t parsed = 0u;
    if (!parse_u32(text, &parsed) || parsed == 0u || parsed > UINT16_MAX) {
        return 0;
    }
    *value = (uint16_t)parsed;
    return 1;
}

static int parse_scenario(const char* text, enum scenario* value) {
    if (strcmp(text, "single") == 0) {
        *value = SCENARIO_SINGLE;
    } else if (strcmp(text, "initial") == 0) {
        *value = SCENARIO_INITIAL;
    } else if (strcmp(text, "survivor") == 0) {
        *value = SCENARIO_SURVIVOR;
    } else if (strcmp(text, "restart") == 0) {
        *value = SCENARIO_RESTART;
    } else {
        return 0;
    }
    return 1;
}

static const char* scenario_name(enum scenario scenario) {
    switch (scenario) {
    case SCENARIO_SINGLE:
        return "single";
    case SCENARIO_INITIAL:
        return "initial";
    case SCENARIO_SURVIVOR:
        return "survivor";
    case SCENARIO_RESTART:
        return "restart";
    }
    return "unknown";
}

static int parse_options(int argc, char** argv, struct options* options) {
    memset(options, 0, sizeof(*options));
    snprintf(options->local_address, sizeof(options->local_address), "%s", "127.0.0.1");
    snprintf(options->remote_address, sizeof(options->remote_address), "%s", "127.0.0.1");
    options->link_id = 42u;
    options->scenario = SCENARIO_SINGLE;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            options->id = argv[++i][0];
            if ((options->id != 'A' && options->id != 'B') || argv[i][1] != '\0') {
                return 0;
            }
        } else if (strcmp(argv[i], "--local-port") == 0 && i + 1 < argc) {
            if (!parse_port(argv[++i], &options->local_port)) {
                return 0;
            }
        } else if (strcmp(argv[i], "--remote-port") == 0 && i + 1 < argc) {
            if (!parse_port(argv[++i], &options->remote_port)) {
                return 0;
            }
        } else if (strcmp(argv[i], "--link-id") == 0 && i + 1 < argc) {
            if (!parse_u32(argv[++i], &options->link_id) || options->link_id == 0u) {
                return 0;
            }
        } else if (strcmp(argv[i], "--local-address") == 0 && i + 1 < argc) {
            if (snprintf(options->local_address, sizeof(options->local_address), "%s", argv[++i]) >=
                (int)sizeof(options->local_address)) {
                return 0;
            }
        } else if (strcmp(argv[i], "--remote-address") == 0 && i + 1 < argc) {
            if (snprintf(options->remote_address, sizeof(options->remote_address), "%s", argv[++i]) >=
                (int)sizeof(options->remote_address)) {
                return 0;
            }
        } else if (strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            if (!parse_scenario(argv[++i], &options->scenario)) {
                return 0;
            }
        } else {
            return 0;
        }
    }

    return options->id != 0 && options->local_port != 0u && options->remote_port != 0u;
}

static void sleep_ms(unsigned milliseconds) {
    struct timespec delay;
    delay.tv_sec = (time_t)(milliseconds / 1000u);
    delay.tv_nsec = (long)(milliseconds % 1000u) * 1000000L;
    (void)nanosleep(&delay, NULL);
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

static uint8_t pattern_byte(char id, unsigned round, size_t index) {
    const unsigned base = id == 'A' ? 0x31u : 0x97u;
    return (uint8_t)((base + round * 17u + (unsigned)(index * 13u)) & 0xffu);
}

static void fill_payload(uint8_t* payload, char id, unsigned round) {
    for (size_t i = 0u; i < PAYLOAD_SIZE; ++i) {
        payload[i] = pattern_byte(id, round, i);
    }
}

static int verify_payload(const uint8_t* payload, char id, unsigned round) {
    for (size_t i = 0u; i < PAYLOAD_SIZE; ++i) {
        if (payload[i] != pattern_byte(id, round, i)) {
            fprintf(stderr,
                    "payload mismatch id=%c round=%u offset=%zu expected=%u actual=%u\n",
                    id, round, i, (unsigned)pattern_byte(id, round, i),
                    (unsigned)payload[i]);
            return 0;
        }
    }
    return 1;
}

static int exchange_round(spw_port_t* port, char id, unsigned round) {
    const char peer_id = id == 'A' ? 'B' : 'A';
    const spw_terminator_t local_terminator =
        id == 'A' ? SPW_TERMINATOR_EOP : SPW_TERMINATOR_EEP;
    const spw_terminator_t peer_terminator =
        peer_id == 'A' ? SPW_TERMINATOR_EOP : SPW_TERMINATOR_EEP;

    uint8_t tx_payload[8192];
    uint8_t rx_payload[8192];
    fill_payload(tx_payload, id, round);
    memset(rx_payload, 0, sizeof(rx_payload));

    spw_packet_t tx = {tx_payload, PAYLOAD_SIZE, PAYLOAD_SIZE, local_terminator};
    spw_result_t result = spw_port_send(port, &tx, IO_TIMEOUT_US);
    if (result != SPW_OK) {
        fprintf(stderr, "round %u packet send failed: %d\n", round, (int)result);
        return 0;
    }

    spw_packet_t rx = {rx_payload, 0u, PAYLOAD_SIZE, SPW_TERMINATOR_EOP};
    result = spw_port_receive(port, &rx, IO_TIMEOUT_US);
    if (result != SPW_OK) {
        fprintf(stderr, "round %u packet receive failed: %d\n", round, (int)result);
        return 0;
    }
    if (rx.length != PAYLOAD_SIZE || rx.terminator != peer_terminator ||
        !verify_payload(rx_payload, peer_id, round)) {
        fprintf(stderr, "round %u received packet metadata/content mismatch\n", round);
        return 0;
    }

    spw_time_code_t tx_time = {
        (uint8_t)((round * 10u + (id == 'A' ? 1u : 2u)) & 0x3fu),
        0u,
    };
    result = spw_port_send_time_code(port, &tx_time, IO_TIMEOUT_US);
    if (result != SPW_OK) {
        fprintf(stderr, "round %u time-code send failed: %d\n", round, (int)result);
        return 0;
    }

    spw_time_code_t rx_time = {0u, 0u};
    result = spw_port_receive_time_code(port, &rx_time, IO_TIMEOUT_US);
    if (result != SPW_OK) {
        fprintf(stderr, "round %u time-code receive failed: %d\n", round, (int)result);
        return 0;
    }
    const uint8_t expected_time =
        (uint8_t)((round * 10u + (peer_id == 'A' ? 1u : 2u)) & 0x3fu);
    if (rx_time.time_count != expected_time || rx_time.control_flags != 0u) {
        fprintf(stderr, "round %u time-code mismatch\n", round);
        return 0;
    }

    printf("ROUND %u OK id=%c bytes=%zu terminator=%s time=%u\n",
           round, id, PAYLOAD_SIZE,
           local_terminator == SPW_TERMINATOR_EEP ? "EEP" : "EOP",
           (unsigned)tx_time.time_count);
    return 1;
}

static int run_scenario(spw_port_t* port, const struct options* options) {
    if (!wait_for_state(port, SPW_LINK_RUN, STATE_TIMEOUT_MS)) {
        fprintf(stderr, "peer did not reach RUN\n");
        return 0;
    }

    if (options->scenario == SCENARIO_RESTART) {
        return exchange_round(port, options->id, 2u);
    }

    if (!exchange_round(port, options->id, 1u)) {
        return 0;
    }

    if (options->scenario != SCENARIO_SURVIVOR) {
        return 1;
    }

    if (!wait_for_state(port, SPW_LINK_ERROR_WAIT, STATE_TIMEOUT_MS)) {
        fprintf(stderr, "survivor did not observe peer loss\n");
        return 0;
    }
    printf("PEER_LOST id=%c\n", options->id);

    if (!wait_for_state(port, SPW_LINK_RUN, STATE_TIMEOUT_MS)) {
        fprintf(stderr, "survivor did not recover peer RUN state\n");
        return 0;
    }
    printf("PEER_RECOVERED id=%c\n", options->id);

    return exchange_round(port, options->id, 2u);
}

int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);

    struct options options;
    if (!parse_options(argc, argv, &options)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    spw_udp_config_t udp =
        SPW_UDP_CONFIG_INITIALIZER(options.local_port, options.remote_port,
                                   options.link_id);
    snprintf(udp.local_address, sizeof(udp.local_address), "%s", options.local_address);
    snprintf(udp.remote_address, sizeof(udp.remote_address), "%s", options.remote_address);
    udp.ack_timeout_ms = 50u;
    udp.max_retries = 5u;
    udp.keepalive_interval_ms = 100u;
    udp.peer_timeout_ms = 500u;

    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
    config.backend_config = &udp;
    config.backend_config_size = sizeof(udp);

    spw_port_t* port = NULL;
    spw_result_t result = spw_port_open(&config, &port);
    if (result != SPW_OK || port == NULL) {
        fprintf(stderr, "spw_port_open failed: %d\n", (int)result);
        return EXIT_FAILURE;
    }

    result = spw_port_start(port);
    if (result != SPW_OK) {
        fprintf(stderr, "spw_port_start failed: %d\n", (int)result);
        (void)spw_port_close(port);
        return EXIT_FAILURE;
    }

    printf("START id=%c scenario=%s local=%s:%u remote=%s:%u link=%u\n",
           options.id, scenario_name(options.scenario), options.local_address,
           (unsigned)options.local_port, options.remote_address,
           (unsigned)options.remote_port, (unsigned)options.link_id);

    const int scenario_ok = run_scenario(port, &options);

    spw_statistics_t statistics;
    memset(&statistics, 0, sizeof(statistics));
    result = spw_port_get_statistics(port, &statistics);
    if (result == SPW_OK) {
        printf("STATS id=%c tx=%llu rx=%llu tx_time=%llu rx_time=%llu link_errors=%llu\n",
               options.id,
               (unsigned long long)statistics.tx_packets,
               (unsigned long long)statistics.rx_packets,
               (unsigned long long)statistics.tx_time_codes,
               (unsigned long long)statistics.rx_time_codes,
               (unsigned long long)statistics.link_errors);
    }

    (void)spw_port_close(port);

    if (!scenario_ok) {
        return EXIT_FAILURE;
    }

    printf("PASS id=%c scenario=%s\n", options.id, scenario_name(options.scenario));
    return EXIT_SUCCESS;
}
