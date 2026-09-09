// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include "profiling/profile.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPW_PROFILE_COMPARISON_MAX_SAMPLES 4096u
#define SPW_PROFILE_COMPARISON_MAX_PAYLOAD 4096u
#define SPW_PROFILE_COMPARISON_WORKSPACE_SIZE 4096u

typedef struct comparison_driver {
    spw_link_state_t state;
    volatile uint64_t sink;
} comparison_driver_t;

typedef union comparison_workspace {
    max_align_t alignment;
    uint8_t bytes[SPW_PROFILE_COMPARISON_WORKSPACE_SIZE];
} comparison_workspace_t;

typedef struct comparison_statistics {
    uint64_t minimum;
    double median;
    double mean;
    uint64_t p95;
    uint64_t p99;
    uint64_t maximum;
    double standard_deviation;
} comparison_statistics_t;

static uint64_t g_native_samples[SPW_PROFILE_COMPARISON_MAX_SAMPLES];
static uint64_t g_spwkit_samples[SPW_PROFILE_COMPARISON_MAX_SAMPLES];
static uint8_t g_payload[SPW_PROFILE_COMPARISON_MAX_PAYLOAD];
static comparison_workspace_t g_workspace;

static spw_result_t comparison_provider_submit(comparison_driver_t* driver,
                                               const spw_packet_t* packet) {
    if (driver == NULL || packet == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (driver->state != SPW_LINK_RUN) {
        return SPW_ERR_INVALID_STATE;
    }
    if (packet->length > SPW_PROFILE_COMPARISON_MAX_PAYLOAD ||
        (packet->length != 0u && packet->data == NULL)) {
        return SPW_ERR_INVALID_PACKET;
    }

    /*
     * Shared native/provider handoff. A real platform adapts this exact point
     * to its DMA/MMIO/AXI/vendor API submission boundary.
     */
    SPW_PROFILE_TX_PROVIDER_BOUNDARY();

    /* Keep the call observable without adding work before the measured handoff. */
    if (packet->length != 0u) {
        driver->sink += packet->data[0u];
        driver->sink += packet->data[packet->length - 1u];
    } else {
        driver->sink += 1u;
    }
    return SPW_OK;
}

static spw_result_t comparison_native_send(comparison_driver_t* driver,
                                           const spw_packet_t* packet,
                                           spw_timeout_us_t timeout_us) {
    (void)timeout_us;

    /* Equivalent direct/native API-entry boundary. */
    SPW_PROFILE_CAPTURE_START();
    return comparison_provider_submit(driver, packet);
}

static spw_result_t comparison_start(void* raw) {
    comparison_driver_t* driver = (comparison_driver_t*)raw;
    driver->state = SPW_LINK_RUN;
    return SPW_OK;
}

static spw_result_t comparison_stop(void* raw) {
    comparison_driver_t* driver = (comparison_driver_t*)raw;
    driver->state = SPW_LINK_READY;
    return SPW_OK;
}

static spw_result_t comparison_reset(void* raw) {
    comparison_driver_t* driver = (comparison_driver_t*)raw;
    driver->state = SPW_LINK_ERROR_RESET;
    driver->sink = 0u;
    return SPW_OK;
}

static spw_result_t comparison_get_link_state(const void* raw,
                                              spw_link_state_t* out_state) {
    const comparison_driver_t* driver = (const comparison_driver_t*)raw;
    *out_state = driver->state;
    return SPW_OK;
}

static spw_result_t comparison_get_capabilities(
    const void* raw,
    spw_capabilities_t* out_capabilities) {
    (void)raw;
    memset(out_capabilities, 0, sizeof(*out_capabilities));
    out_capabilities->max_packet_size = SPW_PROFILE_COMPARISON_MAX_PAYLOAD;
    out_capabilities->tx_queue_depth = 1u;
    out_capabilities->rx_queue_depth = 1u;
    out_capabilities->buffer_alignment = 1u;
    return SPW_OK;
}

static spw_result_t comparison_driver_send(void* raw,
                                           const spw_packet_t* packet,
                                           spw_timeout_us_t timeout_us) {
    (void)timeout_us;
    return comparison_provider_submit((comparison_driver_t*)raw, packet);
}

static spw_result_t comparison_receive(void* raw,
                                       spw_packet_t* packet,
                                       spw_timeout_us_t timeout_us) {
    (void)raw;
    (void)packet;
    (void)timeout_us;
    return SPW_ERR_TIMEOUT;
}

static const spw_driver_ops_t COMPARISON_DRIVER_OPS = {
    .struct_size = sizeof(spw_driver_ops_t),
    .version = SPW_DRIVER_OPS_VERSION,
    .start = comparison_start,
    .stop = comparison_stop,
    .reset = comparison_reset,
    .get_link_state = comparison_get_link_state,
    .get_capabilities = comparison_get_capabilities,
    .send = comparison_driver_send,
    .receive = comparison_receive,
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

static comparison_statistics_t calculate_statistics(uint64_t* samples,
                                                     size_t count) {
    comparison_statistics_t statistics;
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

static int sample_last_delta(uint64_t* out_delta) {
    const volatile spw_profile_sample_t* sample = spw_profile_last_sample();
    if (out_delta == NULL || sample == NULL || sample->sequence == 0u) {
        return 0;
    }
    *out_delta = sample->delta;
    return 1;
}

static int run_native_sample(comparison_driver_t* driver,
                             const spw_packet_t* packet,
                             uint64_t* out_delta) {
    if (comparison_native_send(driver, packet, SPW_TIMEOUT_IMMEDIATE) != SPW_OK) {
        return 0;
    }
    return sample_last_delta(out_delta);
}

static int run_spwkit_sample(spw_port_t* port,
                             const spw_packet_t* packet,
                             uint64_t* out_delta) {
    if (spw_port_send(port, packet, SPW_TIMEOUT_IMMEDIATE) != SPW_OK) {
        return 0;
    }
    return sample_last_delta(out_delta);
}

static void print_statistics(const comparison_statistics_t* statistics) {
    printf("{\"min\":%llu", (unsigned long long)statistics->minimum);
    printf(",\"median\":%.3f", statistics->median);
    printf(",\"mean\":%.3f", statistics->mean);
    printf(",\"p95\":%llu", (unsigned long long)statistics->p95);
    printf(",\"p99\":%llu", (unsigned long long)statistics->p99);
    printf(",\"max\":%llu", (unsigned long long)statistics->maximum);
    printf(",\"stddev\":%.3f}", statistics->standard_deviation);
}

static void print_result(size_t payload_size,
                         size_t warmup_iterations,
                         size_t iterations,
                         const comparison_statistics_t* native_statistics,
                         const comparison_statistics_t* spwkit_statistics) {
    const double median_delta = spwkit_statistics->median - native_statistics->median;
    const double mean_delta = spwkit_statistics->mean - native_statistics->mean;
    const long long p95_delta =
        (long long)spwkit_statistics->p95 - (long long)native_statistics->p95;
    const long long p99_delta =
        (long long)spwkit_statistics->p99 - (long long)native_statistics->p99;

    printf("{\"schema\":\"spwkit.profile.comparison.v1\"");
    printf(",\"measurement_domain\":\"software\"");
    printf(",\"unit\":\"counter_ticks\"");
    printf(",\"direction\":\"tx\"");
    printf(",\"backend\":\"driver\"");
    printf(",\"spwkit_path\":\"copied\"");
    printf(",\"native_path\":\"direct-provider-call\"");
    printf(",\"boundary\":\"api-entry-to-provider-native-boundary\"");
    printf(",\"provider_fixture\":\"shared-reference-provider-submit\"");
    printf(",\"sample_order\":\"alternating-per-iteration\"");
    printf(",\"counter_floor_subtracted\":false");
    printf(",\"payload_bytes\":%zu", payload_size);
    printf(",\"warmup_iterations\":%zu", warmup_iterations);
    printf(",\"iterations\":%zu", iterations);
    printf(",\"platform\":{\"architecture\":\"%s\"}", architecture_name());
    printf(",\"compiler\":{\"family\":\"%s\",\"major\":%u,\"minor\":%u}",
           compiler_name(), compiler_major(), compiler_minor());
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

static void print_usage(const char* program) {
    fprintf(stderr,
            "Usage: %s [--warmup N] [--iterations N] [--payload N]\n",
            program);
}

int main(int argc, char** argv) {
    size_t warmup_iterations = 256u;
    size_t iterations = 1024u;
    size_t payload_size = 64u;
    comparison_driver_t driver;
    spw_driver_config_t driver_config;
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DRIVER);
    spw_port_workspace_requirements_t requirements;
    spw_port_t* port = NULL;
    spw_packet_t packet;
    comparison_statistics_t native_statistics;
    comparison_statistics_t spwkit_statistics;
    size_t i;

    for (i = 1u; i < (size_t)argc; ++i) {
        if (strcmp(argv[i], "--warmup") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, 1000000u, &warmup_iterations)) {
                print_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--iterations") == 0 &&
                   i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 1u, SPW_PROFILE_COMPARISON_MAX_SAMPLES,
                            &iterations)) {
                print_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--payload") == 0 &&
                   i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, SPW_PROFILE_COMPARISON_MAX_PAYLOAD,
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
        SPW_DRIVER_CONFIG_INITIALIZER(&COMPARISON_DRIVER_OPS, &driver);
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
        fprintf(stderr, "comparison workspace is insufficient\n");
        return 1;
    }

    if (spw_port_open_in_place(&config,
                               g_workspace.bytes,
                               sizeof(g_workspace.bytes),
                               &port) != SPW_OK ||
        spw_port_start(port) != SPW_OK) {
        fprintf(stderr, "failed to start comparison DRIVER port\n");
        return 1;
    }

    spw_profile_prepare();

    for (i = 0u; i < warmup_iterations; ++i) {
        uint64_t ignored = 0u;
        if ((i & 1u) == 0u) {
            if (!run_native_sample(&driver, &packet, &ignored) ||
                !run_spwkit_sample(port, &packet, &ignored)) {
                fprintf(stderr, "comparison warmup failed\n");
                return 1;
            }
        } else {
            if (!run_spwkit_sample(port, &packet, &ignored) ||
                !run_native_sample(&driver, &packet, &ignored)) {
                fprintf(stderr, "comparison warmup failed\n");
                return 1;
            }
        }
    }

    spw_profile_reset();
    for (i = 0u; i < iterations; ++i) {
        if ((i & 1u) == 0u) {
            if (!run_native_sample(&driver, &packet, &g_native_samples[i]) ||
                !run_spwkit_sample(port, &packet, &g_spwkit_samples[i])) {
                fprintf(stderr, "comparison sample failed\n");
                return 1;
            }
        } else {
            if (!run_spwkit_sample(port, &packet, &g_spwkit_samples[i]) ||
                !run_native_sample(&driver, &packet, &g_native_samples[i])) {
                fprintf(stderr, "comparison sample failed\n");
                return 1;
            }
        }
    }

    native_statistics = calculate_statistics(g_native_samples, iterations);
    spwkit_statistics = calculate_statistics(g_spwkit_samples, iterations);
    print_result(payload_size,
                 warmup_iterations,
                 iterations,
                 &native_statistics,
                 &spwkit_statistics);

    if (spw_port_close(port) != SPW_OK) {
        fprintf(stderr, "failed to close comparison DRIVER port\n");
        return 1;
    }

    return driver.sink == UINT64_MAX ? 1 : 0;
}
