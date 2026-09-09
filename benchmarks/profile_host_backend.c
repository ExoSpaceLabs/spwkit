// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include "profiling/profile.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPW_HOST_BENCHMARK_MAX_SAMPLES 4096u
#define SPW_HOST_BENCHMARK_MAX_PAYLOAD 4096u
#define SPW_HOST_BENCHMARK_WORKSPACE_SIZE 65536u

typedef enum host_backend_kind {
    HOST_BACKEND_LOOPBACK = 0,
    HOST_BACKEND_SIMULATOR = 1
} host_backend_kind_t;

typedef enum host_direction {
    HOST_DIRECTION_TX = 0,
    HOST_DIRECTION_RX = 1
} host_direction_t;

typedef union host_workspace {
    max_align_t alignment;
    uint8_t bytes[SPW_HOST_BENCHMARK_WORKSPACE_SIZE];
} host_workspace_t;

typedef struct benchmark_statistics {
    uint64_t minimum;
    double median;
    double mean;
    uint64_t p95;
    uint64_t p99;
    uint64_t maximum;
    double standard_deviation;
} benchmark_statistics_t;

typedef struct host_fixture {
    host_backend_kind_t backend;
    spw_port_t* tx_port;
    spw_port_t* rx_port;
    host_workspace_t workspace_a;
    host_workspace_t workspace_b;
} host_fixture_t;

static uint64_t g_samples[SPW_HOST_BENCHMARK_MAX_SAMPLES];
static uint8_t g_payload[SPW_HOST_BENCHMARK_MAX_PAYLOAD];
static uint8_t g_receive[SPW_HOST_BENCHMARK_MAX_PAYLOAD];

static int parse_size(const char* text,
                      size_t minimum,
                      size_t maximum,
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

static benchmark_statistics_t calculate_statistics(uint64_t* samples,
                                                   size_t count) {
    benchmark_statistics_t statistics;
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
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__ARM_ARCH_7EM__)
    return "armv7e-m";
#else
    return "unknown";
#endif
}

static const char* compiler_name(void) {
#if defined(__clang__)
    return "clang";
#elif defined(__GNUC__)
    return "gcc";
#elif defined(_MSC_VER)
    return "msvc";
#else
    return "unknown";
#endif
}

static unsigned int compiler_major(void) {
#if defined(__clang__)
    return (unsigned int)__clang_major__;
#elif defined(__GNUC__)
    return (unsigned int)__GNUC__;
#elif defined(_MSC_VER)
    return (unsigned int)(_MSC_VER / 100);
#else
    return 0u;
#endif
}

static unsigned int compiler_minor(void) {
#if defined(__clang__)
    return (unsigned int)__clang_minor__;
#elif defined(__GNUC__)
    return (unsigned int)__GNUC_MINOR__;
#elif defined(_MSC_VER)
    return (unsigned int)(_MSC_VER % 100);
#else
    return 0u;
#endif
}

static const char* backend_name(host_backend_kind_t backend) {
    return backend == HOST_BACKEND_SIMULATOR ? "simulator" : "loopback";
}

static const char* direction_name(host_direction_t direction) {
    return direction == HOST_DIRECTION_RX ? "rx" : "tx";
}

static int open_port(const spw_port_config_t* config,
                     host_workspace_t* workspace,
                     spw_port_t** out_port) {
    spw_port_workspace_requirements_t requirements;
    if (spw_port_workspace_requirements(config, &requirements) != SPW_OK ||
        requirements.size > sizeof(workspace->bytes) ||
        requirements.alignment > _Alignof(host_workspace_t)) {
        return 0;
    }
    if (spw_port_open_in_place(config, workspace->bytes,
                               sizeof(workspace->bytes), out_port) != SPW_OK) {
        return 0;
    }
    return 1;
}

static int fixture_open(host_fixture_t* fixture, host_backend_kind_t backend) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->backend = backend;

    if (backend == HOST_BACKEND_LOOPBACK) {
        spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
        if (!open_port(&config, &fixture->workspace_a, &fixture->tx_port) ||
            spw_port_start(fixture->tx_port) != SPW_OK) {
            return 0;
        }
        fixture->rx_port = fixture->tx_port;
        return 1;
    }

    {
        spw_simulator_config_t sim_a = SPW_SIMULATOR_CONFIG_INITIALIZER;
        spw_simulator_config_t sim_b = SPW_SIMULATOR_CONFIG_INITIALIZER;
        spw_port_config_t config_a = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
        spw_port_config_t config_b = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
        sim_a.link_id = 0x53505750424e4348ull;
        sim_a.endpoint = SPW_SIMULATOR_ENDPOINT_A;
        sim_b.link_id = sim_a.link_id;
        sim_b.endpoint = SPW_SIMULATOR_ENDPOINT_B;
        config_a.backend_config = &sim_a;
        config_a.backend_config_size = sizeof(sim_a);
        config_b.backend_config = &sim_b;
        config_b.backend_config_size = sizeof(sim_b);
        if (!open_port(&config_a, &fixture->workspace_a, &fixture->tx_port) ||
            !open_port(&config_b, &fixture->workspace_b, &fixture->rx_port)) {
            return 0;
        }
        if (spw_port_start(fixture->tx_port) != SPW_OK ||
            spw_port_start(fixture->rx_port) != SPW_OK) {
            return 0;
        }
    }
    return 1;
}

static int fixture_close(host_fixture_t* fixture) {
    int ok = 1;
    if (fixture->backend == HOST_BACKEND_LOOPBACK) {
        if (fixture->tx_port != NULL) {
            ok = spw_port_stop(fixture->tx_port) == SPW_OK && ok;
            ok = spw_port_close(fixture->tx_port) == SPW_OK && ok;
        }
        return ok;
    }
    if (fixture->tx_port != NULL) {
        ok = spw_port_stop(fixture->tx_port) == SPW_OK && ok;
        ok = spw_port_close(fixture->tx_port) == SPW_OK && ok;
    }
    if (fixture->rx_port != NULL) {
        ok = spw_port_stop(fixture->rx_port) == SPW_OK && ok;
        ok = spw_port_close(fixture->rx_port) == SPW_OK && ok;
    }
    return ok;
}

static int send_packet(spw_port_t* port, size_t payload_size) {
    spw_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.data = g_payload;
    packet.length = payload_size;
    packet.capacity = payload_size;
    packet.terminator = SPW_TERMINATOR_EOP;
    return spw_port_send(port, &packet, SPW_TIMEOUT_IMMEDIATE) == SPW_OK;
}

static int receive_packet(spw_port_t* port, size_t payload_size) {
    spw_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.data = g_receive;
    packet.capacity = sizeof(g_receive);
    if (spw_port_receive(port, &packet, SPW_TIMEOUT_IMMEDIATE) != SPW_OK) {
        return 0;
    }
    return packet.length == payload_size && packet.terminator == SPW_TERMINATOR_EOP &&
           (payload_size == 0u || memcmp(g_receive, g_payload, payload_size) == 0);
}

static int run_sample(host_fixture_t* fixture,
                      host_direction_t direction,
                      size_t payload_size,
                      uint64_t* out_delta) {
    uint64_t start;
    uint64_t end;
    int ok;
    if (direction == HOST_DIRECTION_TX) {
        start = spw_profile_counter_read();
        ok = send_packet(fixture->tx_port, payload_size);
        end = spw_profile_counter_read();
        if (!ok || !receive_packet(fixture->rx_port, payload_size)) {
            return 0;
        }
    } else {
        if (!send_packet(fixture->tx_port, payload_size)) {
            return 0;
        }
        start = spw_profile_counter_read();
        ok = receive_packet(fixture->rx_port, payload_size);
        end = spw_profile_counter_read();
        if (!ok) {
            return 0;
        }
    }
    *out_delta = spw_profile_counter_delta(start, end);
    return 1;
}

static void print_statistics(const benchmark_statistics_t* statistics) {
    printf("{\"min\":%llu", (unsigned long long)statistics->minimum);
    printf(",\"median\":%.3f", statistics->median);
    printf(",\"mean\":%.3f", statistics->mean);
    printf(",\"p95\":%llu", (unsigned long long)statistics->p95);
    printf(",\"p99\":%llu", (unsigned long long)statistics->p99);
    printf(",\"max\":%llu", (unsigned long long)statistics->maximum);
    printf(",\"stddev\":%.3f}", statistics->standard_deviation);
}

static void print_result(host_backend_kind_t backend,
                         host_direction_t direction,
                         size_t payload_size,
                         size_t warmup_iterations,
                         size_t iterations,
                         const benchmark_statistics_t* statistics) {
    printf("{\"schema\":\"spwkit.profile.backend.v1\"");
    printf(",\"measurement_domain\":\"software\"");
    printf(",\"unit\":\"counter_ticks\"");
    printf(",\"backend\":\"%s\"", backend_name(backend));
    printf(",\"direction\":\"%s\"", direction_name(direction));
    printf(",\"path\":\"standard\"");
    printf(",\"boundary\":\"complete-public-api-operation\"");
    printf(",\"carrier\":\"in-memory-queue\"");
    printf(",\"counter_floor_subtracted\":false");
    printf(",\"payload_bytes\":%zu", payload_size);
    printf(",\"warmup_iterations\":%zu", warmup_iterations);
    printf(",\"iterations\":%zu", iterations);
    printf(",\"heap_used\":false");
    printf(",\"sample_storage\":\"fixed\"");
    printf(",\"platform\":{\"architecture\":\"%s\"}", architecture_name());
    printf(",\"compiler\":{\"family\":\"%s\",\"major\":%u,\"minor\":%u}",
           compiler_name(), compiler_major(), compiler_minor());
    printf(",\"counter\":{\"kind\":\"%s\",\"width_bits\":%u,\"frequency_hz\":%llu}",
           spw_profile_counter_kind(),
           (unsigned int)spw_profile_counter_width_bits(),
           (unsigned long long)spw_profile_counter_frequency_hz());
    printf(",\"statistics\":");
    print_statistics(statistics);
    printf("}\n");
}

static void print_usage(const char* program) {
    fprintf(stderr,
            "Usage: %s --backend loopback|simulator --direction tx|rx "
            "[--warmup N] [--iterations N] [--payload N] [--probe-smoke]\n",
            program);
}

int main(int argc, char** argv) {
    host_backend_kind_t backend = HOST_BACKEND_LOOPBACK;
    host_direction_t direction = HOST_DIRECTION_TX;
    int have_backend = 0;
    int have_direction = 0;
    int probe_smoke = 0;
    size_t warmup_iterations = 256u;
    size_t iterations = 1024u;
    size_t payload_size = 64u;
    host_fixture_t fixture;
    benchmark_statistics_t statistics;
    size_t i;

    for (i = 1u; i < (size_t)argc; ++i) {
        if (strcmp(argv[i], "--backend") == 0 && i + 1u < (size_t)argc) {
            const char* value = argv[++i];
            if (strcmp(value, "loopback") == 0) {
                backend = HOST_BACKEND_LOOPBACK;
            } else if (strcmp(value, "simulator") == 0) {
                backend = HOST_BACKEND_SIMULATOR;
            } else {
                print_usage(argv[0]);
                return 2;
            }
            have_backend = 1;
        } else if (strcmp(argv[i], "--direction") == 0 && i + 1u < (size_t)argc) {
            const char* value = argv[++i];
            if (strcmp(value, "tx") == 0) {
                direction = HOST_DIRECTION_TX;
            } else if (strcmp(value, "rx") == 0) {
                direction = HOST_DIRECTION_RX;
            } else {
                print_usage(argv[0]);
                return 2;
            }
            have_direction = 1;
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, 1000000u, &warmup_iterations)) {
                print_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--iterations") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 1u, SPW_HOST_BENCHMARK_MAX_SAMPLES, &iterations)) {
                print_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--payload") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, SPW_HOST_BENCHMARK_MAX_PAYLOAD, &payload_size)) {
                print_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--probe-smoke") == 0) {
            probe_smoke = 1;
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }

    if (!have_backend || !have_direction) {
        print_usage(argv[0]);
        return 2;
    }

    for (i = 0u; i < payload_size; ++i) {
        g_payload[i] = (uint8_t)((i * 29u + 7u) & 0xffu);
    }
    memset(g_receive, 0, sizeof(g_receive));

    if (!fixture_open(&fixture, backend)) {
        fprintf(stderr, "failed to open %s benchmark fixture\n", backend_name(backend));
        return 1;
    }

    spw_profile_prepare();
    if (probe_smoke) {
        uint64_t ignored = 0u;
        const volatile spw_profile_sample_t* sample;
        spw_profile_reset();
        if (!run_sample(&fixture, direction, payload_size, &ignored)) {
            fprintf(stderr, "probe smoke operation failed\n");
            return 1;
        }
        sample = spw_profile_last_sample();
        if (sample == NULL || sample->sequence != 1u) {
            fprintf(stderr, "probe smoke expected sequence=1, got %u\n",
                    sample == NULL ? 0u : sample->sequence);
            return 1;
        }
        printf("PROBE_SMOKE PASS backend=%s direction=%s sequence=%u delta=%llu\n",
               backend_name(backend), direction_name(direction), sample->sequence,
               (unsigned long long)sample->delta);
        return fixture_close(&fixture) ? 0 : 1;
    }
    for (i = 0u; i < warmup_iterations; ++i) {
        uint64_t ignored;
        if (!run_sample(&fixture, direction, payload_size, &ignored)) {
            fprintf(stderr, "warmup failed at iteration %zu\n", i);
            return 1;
        }
    }
    for (i = 0u; i < iterations; ++i) {
        if (!run_sample(&fixture, direction, payload_size, &g_samples[i])) {
            fprintf(stderr, "measurement failed at iteration %zu\n", i);
            return 1;
        }
    }

    statistics = calculate_statistics(g_samples, iterations);
    print_result(backend, direction, payload_size,
                 warmup_iterations, iterations, &statistics);

    if (!fixture_close(&fixture)) {
        fprintf(stderr, "failed to close benchmark fixture\n");
        return 1;
    }
    return 0;
}
