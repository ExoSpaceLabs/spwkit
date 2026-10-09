// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L

#include <spwkit/spwkit.h>
#include <spwkit/udp.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PERF_MAX_PAYLOAD (1024u * 1024u)
#define PERF_TIMEOUT_US UINT64_C(5000000)
#define PERF_STATE_TIMEOUT_MS 15000u

typedef enum perf_role {
    PERF_ROLE_SOURCE = 0,
    PERF_ROLE_SINK = 1
} perf_role_t;

typedef struct perf_options {
    perf_role_t role;
    char local_address[SPW_UDP_ADDRESS_MAX];
    char remote_address[SPW_UDP_ADDRESS_MAX];
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t link_id;
    size_t payload_size;
    uint64_t total_bytes;
    uint32_t seed;
} perf_options_t;

static uint64_t monotonic_ns(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &now) != 0) return 0u;
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) +
           (uint64_t)now.tv_nsec;
}

static void sleep_ms(unsigned ms) {
    struct timespec delay = {
        (time_t)(ms / 1000u),
        (long)(ms % 1000u) * 1000000L
    };
    (void)nanosleep(&delay, NULL);
}

static int parse_u64(const char* text, uint64_t* value) {
    char* end = NULL;
    unsigned long long parsed;
    if (text == NULL || text[0] == '\0' || value == NULL) return 0;
    errno = 0;
    parsed = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') return 0;
    *value = (uint64_t)parsed;
    return 1;
}

static int parse_port(const char* text, uint16_t* value) {
    uint64_t parsed = 0u;
    if (!parse_u64(text, &parsed) || parsed == 0u || parsed > UINT16_MAX) {
        return 0;
    }
    *value = (uint16_t)parsed;
    return 1;
}

static void usage(const char* program) {
    fprintf(stderr,
            "usage: %s --role source|sink --local-port PORT "
            "--remote-port PORT [options]\n"
            "  --local-address IPv4   default 127.0.0.1\n"
            "  --remote-address IPv4  default 127.0.0.1\n"
            "  --link-id N            default 264\n"
            "  --payload-size N       default 4096, max 1048576\n"
            "  --total-bytes N        default 1073741824 (1 GiB)\n"
            "  --seed N               default 264\n",
            program);
}

static int parse_options(int argc, char** argv, perf_options_t* options) {
    int role_set = 0;
    int i;
    memset(options, 0, sizeof(*options));
    (void)snprintf(options->local_address, sizeof(options->local_address),
                   "%s", "127.0.0.1");
    (void)snprintf(options->remote_address, sizeof(options->remote_address),
                   "%s", "127.0.0.1");
    options->link_id = 264u;
    options->payload_size = 4096u;
    options->total_bytes = UINT64_C(1073741824);
    options->seed = 264u;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--role") == 0 && i + 1 < argc) {
            const char* role = argv[++i];
            if (strcmp(role, "source") == 0) {
                options->role = PERF_ROLE_SOURCE;
            } else if (strcmp(role, "sink") == 0) {
                options->role = PERF_ROLE_SINK;
            } else {
                return 0;
            }
            role_set = 1;
        } else if (strcmp(argv[i], "--local-port") == 0 && i + 1 < argc) {
            if (!parse_port(argv[++i], &options->local_port)) return 0;
        } else if (strcmp(argv[i], "--remote-port") == 0 && i + 1 < argc) {
            if (!parse_port(argv[++i], &options->remote_port)) return 0;
        } else if (strcmp(argv[i], "--local-address") == 0 && i + 1 < argc) {
            if (snprintf(options->local_address, sizeof(options->local_address),
                         "%s", argv[++i]) >=
                (int)sizeof(options->local_address)) return 0;
        } else if (strcmp(argv[i], "--remote-address") == 0 && i + 1 < argc) {
            if (snprintf(options->remote_address,
                         sizeof(options->remote_address),
                         "%s", argv[++i]) >=
                (int)sizeof(options->remote_address)) return 0;
        } else if (strcmp(argv[i], "--link-id") == 0 && i + 1 < argc) {
            uint64_t value = 0u;
            if (!parse_u64(argv[++i], &value) ||
                value == 0u || value > UINT32_MAX) return 0;
            options->link_id = (uint32_t)value;
        } else if (strcmp(argv[i], "--payload-size") == 0 && i + 1 < argc) {
            uint64_t value = 0u;
            if (!parse_u64(argv[++i], &value) ||
                value == 0u || value > PERF_MAX_PAYLOAD) return 0;
            options->payload_size = (size_t)value;
        } else if (strcmp(argv[i], "--total-bytes") == 0 && i + 1 < argc) {
            if (!parse_u64(argv[++i], &options->total_bytes) ||
                options->total_bytes == 0u) return 0;
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            uint64_t value = 0u;
            if (!parse_u64(argv[++i], &value) || value > UINT32_MAX) return 0;
            options->seed = (uint32_t)value;
        } else {
            return 0;
        }
    }
    return role_set && options->local_port != 0u && options->remote_port != 0u;
}

static uint8_t pattern_byte(uint32_t seed, uint64_t sequence, size_t index) {
    uint64_t value = (uint64_t)seed * UINT64_C(0x9e3779b1) +
                     sequence * UINT64_C(0x85ebca6b) +
                     (uint64_t)index * UINT64_C(0xc2b2ae35);
    value ^= value >> 33u;
    value *= UINT64_C(0xff51afd7ed558ccd);
    value ^= value >> 29u;
    return (uint8_t)value;
}

static void fill_payload(uint8_t* payload, size_t size,
                         uint32_t seed, uint64_t sequence) {
    size_t i;
    for (i = 0u; i < size; ++i) {
        payload[i] = pattern_byte(seed, sequence, i);
    }
}

static int verify_payload(const uint8_t* payload, size_t size,
                          uint32_t seed, uint64_t sequence) {
    size_t i;
    for (i = 0u; i < size; ++i) {
        const uint8_t expected = pattern_byte(seed, sequence, i);
        if (payload[i] != expected) {
            fprintf(stderr,
                    "payload mismatch sequence=%llu offset=%zu "
                    "expected=%u actual=%u\n",
                    (unsigned long long)sequence, i,
                    (unsigned)expected, (unsigned)payload[i]);
            return 0;
        }
    }
    return 1;
}

static int wait_run(spw_port_t* port) {
    unsigned i;
    for (i = 0u; i <= PERF_STATE_TIMEOUT_MS; ++i) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(port, &state) != SPW_OK) return 0;
        if (state == SPW_LINK_RUN) return 1;
        sleep_ms(1u);
    }
    return 0;
}

static void emit_result(const perf_options_t* options,
                        uint64_t transferred,
                        uint64_t packets,
                        uint64_t elapsed_ns,
                        const spw_statistics_t* stats) {
    const long double seconds =
        (long double)elapsed_ns / 1000000000.0L;
    const long double mbps =
        seconds > 0.0L
            ? ((long double)transferred * 8.0L / 1000000.0L) / seconds
            : 0.0L;
    printf("{\"schema\":\"spwkit.transport.udp-throughput.v1\""
           ",\"role\":\"%s\""
           ",\"link_id\":%u"
           ",\"payload_bytes\":%zu"
           ",\"total_bytes\":%llu"
           ",\"packets\":%llu"
           ",\"elapsed_ns\":%llu"
           ",\"payload_mbps\":%.6Lf"
           ",\"tx_packets\":%llu"
           ",\"rx_packets\":%llu"
           ",\"tx_bytes\":%llu"
           ",\"rx_bytes\":%llu"
           ",\"link_errors\":%llu}\n",
           options->role == PERF_ROLE_SOURCE ? "source" : "sink",
           options->link_id,
           options->payload_size,
           (unsigned long long)transferred,
           (unsigned long long)packets,
           (unsigned long long)elapsed_ns,
           mbps,
           (unsigned long long)stats->tx_packets,
           (unsigned long long)stats->rx_packets,
           (unsigned long long)stats->tx_bytes,
           (unsigned long long)stats->rx_bytes,
           (unsigned long long)stats->link_errors);
    fflush(stdout);
}

int main(int argc, char** argv) {
    perf_options_t options;
    spw_udp_config_t udp;
    spw_port_config_t config =
        SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
    spw_port_t* port = NULL;
    uint8_t* payload = NULL;
    uint64_t transferred = 0u;
    uint64_t packets = 0u;
    uint64_t sequence = 0u;
    uint64_t start_ns;
    spw_result_t result;
    spw_statistics_t stats = {0};

    if (!parse_options(argc, argv, &options)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    payload = (uint8_t*)malloc(options.payload_size);
    if (payload == NULL) {
        fprintf(stderr, "failed to allocate %zu-byte payload buffer\n",
                options.payload_size);
        return EXIT_FAILURE;
    }

    udp = (spw_udp_config_t)
        SPW_UDP_CONFIG_INITIALIZER(options.local_port,
                                   options.remote_port,
                                   options.link_id);
    (void)snprintf(udp.local_address, sizeof(udp.local_address),
                   "%s", options.local_address);
    (void)snprintf(udp.remote_address, sizeof(udp.remote_address),
                   "%s", options.remote_address);
    udp.ack_timeout_ms = 50u;
    udp.max_retries = 5u;
    udp.keepalive_interval_ms = 100u;
    udp.peer_timeout_ms = 1000u;

    config.backend_config = &udp;
    config.backend_config_size = sizeof(udp);

    result = spw_port_open(&config, &port);
    if (result != SPW_OK || port == NULL) {
        fprintf(stderr, "spw_port_open failed: %d\n", (int)result);
        free(payload);
        return EXIT_FAILURE;
    }
    result = spw_port_start(port);
    if (result != SPW_OK || !wait_run(port)) {
        fprintf(stderr, "failed to establish UDP VSPW RUN\n");
        (void)spw_port_close(port);
        free(payload);
        return EXIT_FAILURE;
    }

    start_ns = monotonic_ns();
    while (transferred < options.total_bytes) {
        const uint64_t remaining = options.total_bytes - transferred;
        const size_t size = remaining < (uint64_t)options.payload_size
                                ? (size_t)remaining
                                : options.payload_size;

        if (options.role == PERF_ROLE_SOURCE) {
            spw_packet_t packet;
            fill_payload(payload, size, options.seed, sequence);
            packet = (spw_packet_t){
                payload, size, size, SPW_TERMINATOR_EOP
            };
            result = spw_port_send(port, &packet, PERF_TIMEOUT_US);
            if (result != SPW_OK) {
                fprintf(stderr, "send failed sequence=%llu result=%d\n",
                        (unsigned long long)sequence, (int)result);
                (void)spw_port_close(port);
                free(payload);
                return EXIT_FAILURE;
            }
        } else {
            spw_packet_t packet = {
                payload, 0u, options.payload_size, SPW_TERMINATOR_EEP
            };
            result = spw_port_receive(port, &packet, PERF_TIMEOUT_US);
            if (result != SPW_OK || packet.length != size ||
                packet.terminator != SPW_TERMINATOR_EOP ||
                !verify_payload(payload, size, options.seed, sequence)) {
                fprintf(stderr,
                        "receive verification failed sequence=%llu "
                        "result=%d length=%zu expected=%zu\n",
                        (unsigned long long)sequence, (int)result,
                        packet.length, size);
                (void)spw_port_close(port);
                free(payload);
                return EXIT_FAILURE;
            }
        }

        transferred += (uint64_t)size;
        ++packets;
        ++sequence;
    }

    {
        const uint64_t elapsed_ns = monotonic_ns() - start_ns;
        result = spw_port_get_statistics(port, &stats);
        if (result != SPW_OK) {
            fprintf(stderr, "statistics failed: %d\n", (int)result);
            (void)spw_port_close(port);
            free(payload);
            return EXIT_FAILURE;
        }
        emit_result(&options, transferred, packets, elapsed_ns, &stats);
    }

    (void)spw_port_close(port);
    free(payload);
    return EXIT_SUCCESS;
}
