// SPDX-License-Identifier: Apache-2.0

#include "profiling/profile.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPW_PROFILE_FLOOR_MAX_SAMPLES 4096u

typedef struct floor_statistics {
    uint64_t minimum;
    double median;
    double mean;
    uint64_t p95;
    uint64_t p99;
    uint64_t maximum;
    double standard_deviation;
} floor_statistics_t;

static uint64_t g_samples[SPW_PROFILE_FLOOR_MAX_SAMPLES];
static volatile uint64_t g_sink;

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

static floor_statistics_t calculate_statistics(uint64_t* samples,
                                               size_t count) {
    floor_statistics_t statistics;
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
        statistics.median =
            ((double)samples[count / 2u - 1u] +
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

static void print_usage(const char* program) {
    fprintf(stderr,
            "Usage: %s [--warmup N] [--iterations N]\n",
            program);
}

int main(int argc, char** argv) {
    size_t warmup_iterations = 256u;
    size_t iterations = 1024u;
    floor_statistics_t statistics;
    size_t i;

    for (i = 1u; i < (size_t)argc; ++i) {
        if (strcmp(argv[i], "--warmup") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, 1000000u, &warmup_iterations)) {
                print_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--iterations") == 0 &&
                   i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 1u, SPW_PROFILE_FLOOR_MAX_SAMPLES,
                            &iterations)) {
                print_usage(argv[0]);
                return 2;
            }
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }

    spw_profile_prepare();

    for (i = 0u; i < warmup_iterations; ++i) {
        const uint64_t start = spw_profile_counter_read();
        const uint64_t end = spw_profile_counter_read();
        g_sink ^= spw_profile_counter_delta(start, end);
    }

    for (i = 0u; i < iterations; ++i) {
        const uint64_t start = spw_profile_counter_read();
        const uint64_t end = spw_profile_counter_read();
        g_samples[i] = spw_profile_counter_delta(start, end);
    }

    statistics = calculate_statistics(g_samples, iterations);

    printf("{\"schema\":\"spwkit.profile.calibration.v1\"");
    printf(",\"measurement_domain\":\"instrumentation\"");
    printf(",\"unit\":\"counter_ticks\"");
    printf(",\"method\":\"back_to_back_counter_reads\"");
    printf(",\"warmup_iterations\":%zu", warmup_iterations);
    printf(",\"iterations\":%zu", iterations);
    printf(",\"platform\":{\"architecture\":\"%s\"}", architecture_name());
    printf(",\"compiler\":{\"family\":\"%s\"}", compiler_name());
    printf(",\"counter\":{\"kind\":\"%s\",\"width_bits\":%u,\"frequency_hz\":%llu}",
           spw_profile_counter_kind(),
           (unsigned int)spw_profile_counter_width_bits(),
           (unsigned long long)spw_profile_counter_frequency_hz());
    printf(",\"statistics\":{");
    printf("\"min\":%llu", (unsigned long long)statistics.minimum);
    printf(",\"median\":%.3f", statistics.median);
    printf(",\"mean\":%.3f", statistics.mean);
    printf(",\"p95\":%llu", (unsigned long long)statistics.p95);
    printf(",\"p99\":%llu", (unsigned long long)statistics.p99);
    printf(",\"max\":%llu", (unsigned long long)statistics.maximum);
    printf(",\"stddev\":%.3f}", statistics.standard_deviation);
    printf("}\n");

    return g_sink == UINT64_MAX ? 1 : 0;
}
