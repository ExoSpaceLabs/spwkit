// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include "profiling/profile.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPW_PROFILE_BENCHMARK_MAX_SAMPLES 4096u
#define SPW_PROFILE_BENCHMARK_MAX_PAYLOAD 4096u
#define SPW_PROFILE_BENCHMARK_WORKSPACE_SIZE 4096u

typedef struct benchmark_driver {
    spw_link_state_t state;
    volatile uint64_t sink;
} benchmark_driver_t;

typedef union benchmark_workspace {
    max_align_t alignment;
    uint8_t bytes[SPW_PROFILE_BENCHMARK_WORKSPACE_SIZE];
} benchmark_workspace_t;

typedef struct benchmark_statistics {
    uint64_t minimum;
    double median;
    double mean;
    uint64_t p95;
    uint64_t p99;
    uint64_t maximum;
    double standard_deviation;
} benchmark_statistics_t;

static uint64_t g_samples[SPW_PROFILE_BENCHMARK_MAX_SAMPLES];
static uint8_t g_payload[SPW_PROFILE_BENCHMARK_MAX_PAYLOAD];
static benchmark_workspace_t g_workspace;

static spw_result_t benchmark_start(void* raw) {
    benchmark_driver_t* driver = (benchmark_driver_t*)raw;
    driver->state = SPW_LINK_RUN;
    return SPW_OK;
}

static spw_result_t benchmark_stop(void* raw) {
    benchmark_driver_t* driver = (benchmark_driver_t*)raw;
    driver->state = SPW_LINK_READY;
    return SPW_OK;
}

static spw_result_t benchmark_reset(void* raw) {
    benchmark_driver_t* driver = (benchmark_driver_t*)raw;
    driver->state = SPW_LINK_ERROR_RESET;
    driver->sink = 0u;
    return SPW_OK;
}

static spw_result_t benchmark_get_link_state(const void* raw,
                                             spw_link_state_t* out_state) {
    const benchmark_driver_t* driver = (const benchmark_driver_t*)raw;
    *out_state = driver->state;
    return SPW_OK;
}

static spw_result_t benchmark_get_capabilities(
    const void* raw,
    spw_capabilities_t* out_capabilities) {
    (void)raw;
    memset(out_capabilities, 0, sizeof(*out_capabilities));
    out_capabilities->max_packet_size = SPW_PROFILE_BENCHMARK_MAX_PAYLOAD;
    out_capabilities->tx_queue_depth = 1u;
    out_capabilities->rx_queue_depth = 1u;
    out_capabilities->buffer_alignment = 1u;
    return SPW_OK;
}

static spw_result_t benchmark_send(void* raw,
                                   const spw_packet_t* packet,
                                   spw_timeout_us_t timeout_us) {
    benchmark_driver_t* driver = (benchmark_driver_t*)raw;
    (void)timeout_us;

    if (driver->state != SPW_LINK_RUN) {
        return SPW_ERR_INVALID_STATE;
    }
    if (packet->length > SPW_PROFILE_BENCHMARK_MAX_PAYLOAD ||
        (packet->length != 0u && packet->data == NULL)) {
        return SPW_ERR_INVALID_PACKET;
    }

    /*
     * A hardware provider places this immediately at DMA/MMIO/native
     * submission. The deterministic fixture has no device work beyond it.
     */
    SPW_PROFILE_TX_PROVIDER_BOUNDARY();

    if (packet->length != 0u) {
        driver->sink += packet->data[0u];
        driver->sink += packet->data[packet->length - 1u];
    } else {
        driver->sink += 1u;
    }
    return SPW_OK;
}

static spw_result_t benchmark_receive(void* raw,
                                      spw_packet_t* packet,
                                      spw_timeout_us_t timeout_us) {
    (void)raw;
    (void)packet;
    (void)timeout_us;
    return SPW_ERR_TIMEOUT;
}

static const spw_driver_ops_t BENCHMARK_DRIVER_OPS = {
    .struct_size = sizeof(spw_driver_ops_t),
    .version = SPW_DRIVER_OPS_VERSION,
    .start = benchmark_start,
    .stop = benchmark_stop,
    .reset = benchmark_reset,
    .get_link_state = benchmark_get_link_state,
    .get_capabilities = benchmark_get_capabilities,
    .send = benchmark_send,
    .receive = benchmark_receive,
};

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
        const uint64_t lower = samples[count / 2u - 1u];
        const uint64_t upper = samples[count / 2u];
        statistics.median = ((double)lower + (double)upper) / 2.0;
    }

    for (i = 0u; i < count; ++i) {
        sum += (double)samples[i];
    }
    statistics.mean = sum / (double)count;

    for (i = 0u; i < count; ++i) {
        const double difference = (double)samples[i] - statistics.mean;
        variance_sum += difference * difference;
    }
    statistics.standard_deviation =
        square_root(variance_sum / (double)count);

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

static const char* optimization_name(void) {
#if defined(__OPTIMIZE__)
    return "optimized";
#else
    return "unoptimized";
#endif
}

static void print_result(size_t payload_size,
                         size_t warmup_iterations,
                         size_t iterations,
                         const benchmark_statistics_t* statistics) {
    const uint64_t frequency_hz = spw_profile_counter_frequency_hz();

    printf("{\"schema\":\"spwkit.profile.v1\"");
    printf(",\"measurement_domain\":\"software\"");
    printf(",\"unit\":\"counter_ticks\"");
    printf(",\"backend\":\"driver\"");
    printf(",\"direction\":\"tx\"");
    printf(",\"path\":\"copied\"");
    printf(",\"probe_start_id\":%u", (unsigned int)SPWKIT_PROFILE_START);
    printf(",\"probe_end_id\":%u", (unsigned int)SPWKIT_PROFILE_END);
    printf(",\"payload_bytes\":%zu", payload_size);
    printf(",\"warmup_iterations\":%zu", warmup_iterations);
    printf(",\"iterations\":%zu", iterations);
    printf(",\"sample_storage\":\"fixed\"");
    printf(",\"heap_used\":false");
    printf(",\"platform\":{\"architecture\":\"%s\"}", architecture_name());
    printf(",\"compiler\":{\"family\":\"%s\",\"major\":%u,\"minor\":%u}",
           compiler_name(), compiler_major(), compiler_minor());
    printf(",\"optimization\":\"%s\"", optimization_name());
    printf(",\"counter\":{\"kind\":\"%s\",\"width_bits\":%u,\"frequency_hz\":%llu}",
           spw_profile_counter_kind(),
           (unsigned int)spw_profile_counter_width_bits(),
           (unsigned long long)frequency_hz);
    printf(",\"statistics\":{");
    printf("\"min\":%llu", (unsigned long long)statistics->minimum);
    printf(",\"median\":%.3f", statistics->median);
    printf(",\"mean\":%.3f", statistics->mean);
    printf(",\"p95\":%llu", (unsigned long long)statistics->p95);
    printf(",\"p99\":%llu", (unsigned long long)statistics->p99);
    printf(",\"max\":%llu", (unsigned long long)statistics->maximum);
    printf(",\"stddev\":%.3f}", statistics->standard_deviation);
    if (frequency_hz != 0u) {
        const double mean_ns = statistics->mean * 1000000000.0 /
                               (double)frequency_hz;
        printf(",\"derived_time\":{\"mean_ns\":%.3f}", mean_ns);
    } else {
        printf(",\"derived_time\":{\"mean_ns\":null}");
    }
    printf("}\n");
}

static void print_usage(const char* program) {
    fprintf(stderr,
            "Usage: %s [--warmup N] [--iterations N] [--payload N]\n",
            program);
}

int main(int argc, char** argv) {
    size_t warmup_iterations = 256u;
    size_t iterations = 1024u;
    size_t payload_size = 64u;
    benchmark_driver_t driver;
    spw_driver_config_t driver_config;
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DRIVER);
    spw_port_workspace_requirements_t requirements;
    spw_port_t* port = NULL;
    spw_packet_t packet;
    benchmark_statistics_t statistics;
    size_t i;

    for (i = 1u; i < (size_t)argc; ++i) {
        if (strcmp(argv[i], "--warmup") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, 1000000u, &warmup_iterations)) {
                print_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--iterations") == 0 &&
                   i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 1u, SPW_PROFILE_BENCHMARK_MAX_SAMPLES,
                            &iterations)) {
                print_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--payload") == 0 &&
                   i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, SPW_PROFILE_BENCHMARK_MAX_PAYLOAD,
                            &payload_size)) {
                print_usage(argv[0]);
                return 2;
            }
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }

    memset(&driver, 0, sizeof(driver));
    driver.state = SPW_LINK_READY;
    driver_config = (spw_driver_config_t)
        SPW_DRIVER_CONFIG_INITIALIZER(&BENCHMARK_DRIVER_OPS, &driver);
    config.backend_config = &driver_config;
    config.backend_config_size = sizeof(driver_config);

    for (i = 0u; i < payload_size; ++i) {
        g_payload[i] = (uint8_t)(i & 0xffu);
    }
    packet.data = g_payload;
    packet.length = payload_size;
    packet.capacity = payload_size;
    packet.terminator = SPW_TERMINATOR_EOP;

    if (spw_port_workspace_requirements(&config, &requirements) != SPW_OK ||
        requirements.size > sizeof(g_workspace.bytes) ||
        requirements.alignment > _Alignof(max_align_t)) {
        fprintf(stderr, "benchmark workspace is insufficient\n");
        return 1;
    }

    if (spw_port_open_in_place(&config,
                               g_workspace.bytes,
                               sizeof(g_workspace.bytes),
                               &port) != SPW_OK ||
        spw_port_start(port) != SPW_OK) {
        fprintf(stderr, "failed to start benchmark DRIVER port\n");
        return 1;
    }

    spw_profile_prepare();

    for (i = 0u; i < warmup_iterations; ++i) {
        if (spw_port_send(port, &packet, SPW_TIMEOUT_IMMEDIATE) != SPW_OK) {
            fprintf(stderr, "warmup send failed\n");
            return 1;
        }
    }

    spw_profile_reset();
    for (i = 0u; i < iterations; ++i) {
        const volatile spw_profile_sample_t* sample;
        if (spw_port_send(port, &packet, SPW_TIMEOUT_IMMEDIATE) != SPW_OK) {
            fprintf(stderr, "measured send failed\n");
            return 1;
        }
        sample = spw_profile_last_sample();
        if (sample == NULL || sample->sequence != (uint32_t)(i + 1u)) {
            fprintf(stderr, "profiling sample sequence mismatch\n");
            return 1;
        }
        g_samples[i] = sample->delta;
    }

    statistics = calculate_statistics(g_samples, iterations);
    print_result(payload_size, warmup_iterations, iterations, &statistics);

    if (spw_port_stop(port) != SPW_OK || spw_port_close(port) != SPW_OK) {
        fprintf(stderr, "failed to stop benchmark DRIVER port\n");
        return 1;
    }

    return 0;
}
