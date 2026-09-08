// SPDX-License-Identifier: Apache-2.0

#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <spwkit/spwkit.h>

#include "profiling/profile.h"

#include <arpa/inet.h>
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define SPW_UDP_BENCH_MAX_SAMPLES 4096u
#define SPW_UDP_BENCH_MAX_PAYLOAD 4096u
#define SPW_UDP_BENCH_WORKSPACE_SIZE (4u * 1024u * 1024u)

typedef enum udp_direction {
    UDP_DIRECTION_TX = 0,
    UDP_DIRECTION_RX = 1
} udp_direction_t;

typedef union udp_workspace {
    max_align_t alignment;
    uint8_t bytes[SPW_UDP_BENCH_WORKSPACE_SIZE];
} udp_workspace_t;

typedef struct udp_statistics {
    uint64_t minimum;
    double median;
    double mean;
    uint64_t p95;
    uint64_t p99;
    uint64_t maximum;
    double standard_deviation;
} udp_statistics_t;

typedef struct udp_fixture {
    spw_port_t* a;
    spw_port_t* b;
    udp_workspace_t workspace_a;
    udp_workspace_t workspace_b;
    int raw_a;
    int raw_b;
    struct sockaddr_in raw_b_address;
} udp_fixture_t;

static uint64_t g_native_samples[SPW_UDP_BENCH_MAX_SAMPLES];
static uint64_t g_spwkit_samples[SPW_UDP_BENCH_MAX_SAMPLES];
static uint8_t g_payload[SPW_UDP_BENCH_MAX_PAYLOAD];
static uint8_t g_receive[SPW_UDP_BENCH_MAX_PAYLOAD];
static volatile uint64_t g_sink;

static int parse_size(const char* text, size_t minimum, size_t maximum,
                      size_t* out_value) {
    char* end = NULL;
    unsigned long long value;
    if (text == NULL || text[0] == '\0' || out_value == NULL) {
        return 0;
    }
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        value < minimum || value > maximum) {
        return 0;
    }
    *out_value = (size_t)value;
    return 1;
}

static void sort_samples(uint64_t* samples, size_t count) {
    size_t gap;
    for (gap = count / 2u; gap > 0u; gap /= 2u) {
        size_t i;
        for (i = gap; i < count; ++i) {
            const uint64_t value = samples[i];
            size_t j = i;
            while (j >= gap && samples[j - gap] > value) {
                samples[j] = samples[j - gap];
                j -= gap;
            }
            samples[j] = value;
        }
    }
}

static size_t percentile_index(size_t count, unsigned int percentile) {
    size_t rank = ((size_t)percentile * count + 99u) / 100u;
    if (rank == 0u) {
        rank = 1u;
    }
    if (rank > count) {
        rank = count;
    }
    return rank - 1u;
}

static double square_root(double value) {
    double estimate;
    unsigned int i;
    if (value <= 0.0) {
        return 0.0;
    }
    estimate = value > 1.0 ? value : 1.0;
    for (i = 0u; i < 32u; ++i) {
        estimate = 0.5 * (estimate + value / estimate);
    }
    return estimate;
}

static udp_statistics_t calculate_statistics(uint64_t* samples, size_t count) {
    udp_statistics_t statistics;
    double sum = 0.0;
    double variance_sum = 0.0;
    size_t i;
    memset(&statistics, 0, sizeof(statistics));
    sort_samples(samples, count);
    statistics.minimum = samples[0u];
    statistics.maximum = samples[count - 1u];
    statistics.p95 = samples[percentile_index(count, 95u)];
    statistics.p99 = samples[percentile_index(count, 99u)];
    if ((count & 1u) != 0u) {
        statistics.median = (double)samples[count / 2u];
    } else {
        statistics.median = ((double)samples[count / 2u - 1u] +
                             (double)samples[count / 2u]) / 2.0;
    }
    for (i = 0u; i < count; ++i) {
        sum += (double)samples[i];
    }
    statistics.mean = sum / (double)count;
    for (i = 0u; i < count; ++i) {
        const double difference = (double)samples[i] - statistics.mean;
        variance_sum += difference * difference;
    }
    statistics.standard_deviation = square_root(variance_sum / (double)count);
    return statistics;
}

static const char* architecture_name(void) {
#if defined(__x86_64__)
    return "x86_64";
#elif defined(__i386__)
    return "x86";
#elif defined(__aarch64__)
    return "aarch64";
#else
    return "unknown";
#endif
}

static const char* compiler_name(void) {
#if defined(__clang__)
    return "clang";
#elif defined(__GNUC__)
    return "gcc";
#else
    return "unknown";
#endif
}

static int bind_udp_socket(uint16_t port, int* out_fd) {
    int fd;
    int reuse = 1;
    struct sockaddr_in address;
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return 0;
    }
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (const struct sockaddr*)&address, sizeof(address)) != 0) {
        close(fd);
        return 0;
    }
    *out_fd = fd;
    return 1;
}

static void make_loopback_address(uint16_t port, struct sockaddr_in* out) {
    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons(port);
    out->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
}

static int open_spw_udp(uint16_t local_port, uint16_t remote_port,
                        uint32_t link_id, udp_workspace_t* workspace,
                        spw_port_t** out_port) {
    spw_udp_config_t udp = SPW_UDP_CONFIG_INITIALIZER(local_port, remote_port, link_id);
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
    spw_port_workspace_requirements_t requirements;

    udp.fragment_payload_size = 1200u;
    udp.ack_timeout_ms = 20u;
    udp.max_retries = 3u;
    udp.keepalive_interval_ms = 1000u;
    udp.peer_timeout_ms = 5000u;
    config.backend_config = &udp;
    config.backend_config_size = sizeof(udp);

    if (spw_port_workspace_requirements(&config, &requirements) != SPW_OK ||
        requirements.size > sizeof(workspace->bytes) ||
        requirements.alignment > _Alignof(udp_workspace_t)) {
        return 0;
    }
    return spw_port_open_in_place(&config, workspace->bytes,
                                  sizeof(workspace->bytes), out_port) == SPW_OK;
}

static int wait_for_run(spw_port_t* a, spw_port_t* b) {
    unsigned int attempt;
    for (attempt = 0u; attempt < 200u; ++attempt) {
        spw_link_state_t state_a = SPW_LINK_ERROR_RESET;
        spw_link_state_t state_b = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(a, &state_a) != SPW_OK ||
            spw_port_get_link_state(b, &state_b) != SPW_OK) {
            return 0;
        }
        if (state_a == SPW_LINK_RUN && state_b == SPW_LINK_RUN) {
            return 1;
        }
        {
            struct timespec delay = {0, 1000000L};
            (void)nanosleep(&delay, NULL);
        }
    }
    return 0;
}

static int fixture_open(udp_fixture_t* fixture) {
    const uint16_t base = (uint16_t)(47000u + ((unsigned)getpid() % 1000u) * 4u);
    const uint32_t link_id = 0x42575031u;
    memset(fixture, 0, sizeof(*fixture));
    fixture->raw_a = -1;
    fixture->raw_b = -1;

    if (!open_spw_udp(base, (uint16_t)(base + 1u), link_id,
                      &fixture->workspace_a, &fixture->a) ||
        !open_spw_udp((uint16_t)(base + 1u), base, link_id,
                      &fixture->workspace_b, &fixture->b)) {
        return 0;
    }
    if (spw_port_start(fixture->a) != SPW_OK ||
        spw_port_start(fixture->b) != SPW_OK ||
        !wait_for_run(fixture->a, fixture->b)) {
        return 0;
    }

    if (!bind_udp_socket((uint16_t)(base + 2u), &fixture->raw_a) ||
        !bind_udp_socket((uint16_t)(base + 3u), &fixture->raw_b)) {
        return 0;
    }
    make_loopback_address((uint16_t)(base + 3u), &fixture->raw_b_address);
    return 1;
}

static void fixture_close(udp_fixture_t* fixture) {
    if (fixture->a != NULL) {
        (void)spw_port_stop(fixture->a);
        (void)spw_port_close(fixture->a);
    }
    if (fixture->b != NULL) {
        (void)spw_port_stop(fixture->b);
        (void)spw_port_close(fixture->b);
    }
    if (fixture->raw_a >= 0) {
        close(fixture->raw_a);
    }
    if (fixture->raw_b >= 0) {
        close(fixture->raw_b);
    }
}

static int service_sender_ack(spw_port_t* sender) {
    unsigned int attempt;
    for (attempt = 0u; attempt < 8u; ++attempt) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (spw_port_get_link_state(sender, &state) != SPW_OK) {
            return 0;
        }
        if (state != SPW_LINK_RUN) {
            return 0;
        }
    }
    return 1;
}

static int spw_send(spw_port_t* port, size_t payload_size) {
    spw_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.data = g_payload;
    packet.length = payload_size;
    packet.capacity = payload_size;
    packet.terminator = SPW_TERMINATOR_EOP;
    return spw_port_send(port, &packet, 500000u) == SPW_OK;
}

static int spw_receive(spw_port_t* port, size_t payload_size) {
    spw_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.data = g_receive;
    packet.capacity = sizeof(g_receive);
    if (spw_port_receive(port, &packet, 500000u) != SPW_OK) {
        return 0;
    }
    if (packet.length != payload_size || packet.terminator != SPW_TERMINATOR_EOP) {
        return 0;
    }
    return payload_size == 0u || memcmp(g_receive, g_payload, payload_size) == 0;
}

static int raw_send(const udp_fixture_t* fixture, size_t payload_size) {
    const ssize_t result = sendto(fixture->raw_a, g_payload, payload_size, 0,
                                  (const struct sockaddr*)&fixture->raw_b_address,
                                  sizeof(fixture->raw_b_address));
    return result >= 0 && (size_t)result == payload_size;
}

static int raw_receive(const udp_fixture_t* fixture, size_t payload_size) {
    const ssize_t result = recvfrom(fixture->raw_b, g_receive, sizeof(g_receive),
                                    0, NULL, NULL);
    if (result < 0 || (size_t)result != payload_size) {
        return 0;
    }
    return payload_size == 0u || memcmp(g_receive, g_payload, payload_size) == 0;
}

static int sample_native_tx(udp_fixture_t* fixture, size_t payload_size,
                            uint64_t* out_delta) {
    const uint64_t start = spw_profile_counter_read();
    const int ok = raw_send(fixture, payload_size);
    const uint64_t end = spw_profile_counter_read();
    if (!ok || !raw_receive(fixture, payload_size)) {
        return 0;
    }
    *out_delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int sample_spw_tx(udp_fixture_t* fixture, size_t payload_size,
                         uint64_t* out_delta) {
    const uint64_t start = spw_profile_counter_read();
    const int ok = spw_send(fixture->a, payload_size);
    const uint64_t end = spw_profile_counter_read();
    if (!ok || !spw_receive(fixture->b, payload_size) ||
        !service_sender_ack(fixture->a)) {
        return 0;
    }
    *out_delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int sample_native_rx(udp_fixture_t* fixture, size_t payload_size,
                            uint64_t* out_delta) {
    uint64_t start;
    uint64_t end;
    int ok;
    if (!raw_send(fixture, payload_size)) {
        return 0;
    }
    start = spw_profile_counter_read();
    ok = raw_receive(fixture, payload_size);
    end = spw_profile_counter_read();
    if (!ok) {
        return 0;
    }
    *out_delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int sample_spw_rx(udp_fixture_t* fixture, size_t payload_size,
                         uint64_t* out_delta) {
    uint64_t start;
    uint64_t end;
    int ok;
    if (!spw_send(fixture->a, payload_size)) {
        return 0;
    }
    start = spw_profile_counter_read();
    ok = spw_receive(fixture->b, payload_size);
    end = spw_profile_counter_read();
    if (!ok || !service_sender_ack(fixture->a)) {
        return 0;
    }
    *out_delta = spw_profile_counter_delta(start, end);
    return 1;
}

static int run_pair(udp_fixture_t* fixture, udp_direction_t direction,
                    size_t payload_size, size_t iteration,
                    uint64_t* native_delta, uint64_t* spwkit_delta) {
    int native_ok;
    int spwkit_ok;
    if ((iteration & 1u) == 0u) {
        native_ok = direction == UDP_DIRECTION_TX
                        ? sample_native_tx(fixture, payload_size, native_delta)
                        : sample_native_rx(fixture, payload_size, native_delta);
        spwkit_ok = direction == UDP_DIRECTION_TX
                        ? sample_spw_tx(fixture, payload_size, spwkit_delta)
                        : sample_spw_rx(fixture, payload_size, spwkit_delta);
    } else {
        spwkit_ok = direction == UDP_DIRECTION_TX
                        ? sample_spw_tx(fixture, payload_size, spwkit_delta)
                        : sample_spw_rx(fixture, payload_size, spwkit_delta);
        native_ok = direction == UDP_DIRECTION_TX
                        ? sample_native_tx(fixture, payload_size, native_delta)
                        : sample_native_rx(fixture, payload_size, native_delta);
    }
    return native_ok && spwkit_ok;
}

static void print_statistics(const udp_statistics_t* statistics) {
    printf("{\"min\":%llu", (unsigned long long)statistics->minimum);
    printf(",\"median\":%.3f", statistics->median);
    printf(",\"mean\":%.3f", statistics->mean);
    printf(",\"p95\":%llu", (unsigned long long)statistics->p95);
    printf(",\"p99\":%llu", (unsigned long long)statistics->p99);
    printf(",\"max\":%llu", (unsigned long long)statistics->maximum);
    printf(",\"stddev\":%.3f}", statistics->standard_deviation);
}

static void print_result(udp_direction_t direction, size_t payload_size,
                         size_t warmup_iterations, size_t iterations,
                         const udp_statistics_t* native_statistics,
                         const udp_statistics_t* spwkit_statistics) {
    const double median_delta = spwkit_statistics->median - native_statistics->median;
    const double mean_delta = spwkit_statistics->mean - native_statistics->mean;
    const long long p95_delta = (long long)spwkit_statistics->p95 -
                                (long long)native_statistics->p95;
    const long long p99_delta = (long long)spwkit_statistics->p99 -
                                (long long)native_statistics->p99;
    const char* direction_name = direction == UDP_DIRECTION_TX ? "tx" : "rx";

    printf("{\"schema\":\"spwkit.profile.comparison.v1\"");
    printf(",\"measurement_domain\":\"software\"");
    printf(",\"unit\":\"counter_ticks\"");
    printf(",\"direction\":\"%s\"", direction_name);
    printf(",\"backend\":\"udp\"");
    printf(",\"spwkit_path\":\"vspw-tp\"");
    printf(",\"native_path\":\"direct-udp-socket\"");
    printf(",\"boundary\":\"complete-public-api-operation\"");
    printf(",\"provider_fixture\":\"same-process-loopback-udp\"");
    printf(",\"sample_order\":\"alternating-per-iteration\"");
    printf(",\"counter_floor_subtracted\":false");
    printf(",\"payload_bytes\":%zu", payload_size);
    printf(",\"warmup_iterations\":%zu", warmup_iterations);
    printf(",\"iterations\":%zu", iterations);
    printf(",\"platform\":{\"architecture\":\"%s\"}", architecture_name());
    printf(",\"compiler\":{\"family\":\"%s\"}", compiler_name());
    printf(",\"counter\":{\"kind\":\"%s\",\"width_bits\":%u,\"frequency_hz\":%llu}",
           spw_profile_counter_kind(),
           (unsigned int)spw_profile_counter_width_bits(),
           (unsigned long long)spw_profile_counter_frequency_hz());
    printf(",\"native_statistics\":");
    print_statistics(native_statistics);
    printf(",\"spwkit_statistics\":");
    print_statistics(spwkit_statistics);
    printf(",\"delta\":{\"definition\":\"spwkit-minus-native\"");
    printf(",\"median_ticks\":%.3f", median_delta);
    printf(",\"mean_ticks\":%.3f", mean_delta);
    printf(",\"p95_ticks\":%lld", p95_delta);
    printf(",\"p99_ticks\":%lld", p99_delta);
    if (native_statistics->median != 0.0) {
        printf(",\"median_percent\":%.3f",
               100.0 * median_delta / native_statistics->median);
    } else {
        printf(",\"median_percent\":null");
    }
    if (native_statistics->mean != 0.0) {
        printf(",\"mean_percent\":%.3f",
               100.0 * mean_delta / native_statistics->mean);
    } else {
        printf(",\"mean_percent\":null");
    }
    printf("}}\n");
}

static void usage(const char* program) {
    fprintf(stderr,
            "Usage: %s --direction tx|rx [--warmup N] [--iterations N] [--payload N]\n",
            program);
}

int main(int argc, char** argv) {
    udp_direction_t direction = UDP_DIRECTION_TX;
    int have_direction = 0;
    size_t warmup_iterations = 64u;
    size_t iterations = 256u;
    size_t payload_size = 64u;
    udp_fixture_t fixture;
    udp_statistics_t native_statistics;
    udp_statistics_t spwkit_statistics;
    size_t i;

    for (i = 1u; i < (size_t)argc; ++i) {
        if (strcmp(argv[i], "--direction") == 0 && i + 1u < (size_t)argc) {
            const char* value = argv[++i];
            if (strcmp(value, "tx") == 0) {
                direction = UDP_DIRECTION_TX;
            } else if (strcmp(value, "rx") == 0) {
                direction = UDP_DIRECTION_RX;
            } else {
                usage(argv[0]);
                return 2;
            }
            have_direction = 1;
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, 1000000u, &warmup_iterations)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 1u, SPW_UDP_BENCH_MAX_SAMPLES, &iterations)) {
                usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--payload") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, SPW_UDP_BENCH_MAX_PAYLOAD, &payload_size)) {
                usage(argv[0]);
                return 2;
            }
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (!have_direction) {
        usage(argv[0]);
        return 2;
    }

    for (i = 0u; i < payload_size; ++i) {
        g_payload[i] = (uint8_t)((i * 17u + 3u) & 0xffu);
    }
    memset(g_receive, 0, sizeof(g_receive));

    if (!fixture_open(&fixture)) {
        fprintf(stderr, "failed to open UDP benchmark fixture\n");
        return 1;
    }
    spw_profile_prepare();

    for (i = 0u; i < warmup_iterations; ++i) {
        uint64_t native_delta;
        uint64_t spwkit_delta;
        if (!run_pair(&fixture, direction, payload_size, i,
                      &native_delta, &spwkit_delta)) {
            fprintf(stderr, "UDP warmup failed at iteration %zu\n", i);
            fixture_close(&fixture);
            return 1;
        }
        g_sink ^= native_delta ^ spwkit_delta;
    }

    for (i = 0u; i < iterations; ++i) {
        if (!run_pair(&fixture, direction, payload_size, i,
                      &g_native_samples[i], &g_spwkit_samples[i])) {
            fprintf(stderr, "UDP measurement failed at iteration %zu\n", i);
            fixture_close(&fixture);
            return 1;
        }
    }

    native_statistics = calculate_statistics(g_native_samples, iterations);
    spwkit_statistics = calculate_statistics(g_spwkit_samples, iterations);
    print_result(direction, payload_size, warmup_iterations, iterations,
                 &native_statistics, &spwkit_statistics);
    fixture_close(&fixture);
    return g_sink == UINT64_MAX ? 1 : 0;
}
