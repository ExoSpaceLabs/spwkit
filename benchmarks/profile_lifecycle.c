// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include "profiling/profile.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPW_LIFECYCLE_MAX_SAMPLES 4096u
#define SPW_LIFECYCLE_WORKSPACE_SIZE 65536u

typedef union lifecycle_workspace {
    max_align_t alignment;
    uint8_t bytes[SPW_LIFECYCLE_WORKSPACE_SIZE];
} lifecycle_workspace_t;

typedef struct lifecycle_statistics {
    uint64_t minimum;
    double median;
    double mean;
    uint64_t p95;
    uint64_t p99;
    uint64_t maximum;
    double standard_deviation;
} lifecycle_statistics_t;

typedef enum lifecycle_operation {
    LIFECYCLE_OPEN_HEAP = 0,
    LIFECYCLE_OPEN_IN_PLACE,
    LIFECYCLE_START,
    LIFECYCLE_STOP,
    LIFECYCLE_RESET,
    LIFECYCLE_CLOSE_IN_PLACE,
    LIFECYCLE_CLOSE_HEAP,
    LIFECYCLE_OPERATION_COUNT
} lifecycle_operation_t;

static uint64_t g_samples[SPW_LIFECYCLE_MAX_SAMPLES];
static lifecycle_workspace_t g_workspace;
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

static lifecycle_statistics_t calculate_statistics(uint64_t* samples,
                                                   size_t count) {
    lifecycle_statistics_t statistics;
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

static const char* operation_name(lifecycle_operation_t operation) {
    switch (operation) {
    case LIFECYCLE_OPEN_HEAP:
        return "open_heap";
    case LIFECYCLE_OPEN_IN_PLACE:
        return "open_in_place";
    case LIFECYCLE_START:
        return "start";
    case LIFECYCLE_STOP:
        return "stop";
    case LIFECYCLE_RESET:
        return "reset";
    case LIFECYCLE_CLOSE_IN_PLACE:
        return "close_in_place";
    case LIFECYCLE_CLOSE_HEAP:
        return "close_heap";
    default:
        return "unknown";
    }
}

static const char* allocation_name(lifecycle_operation_t operation) {
    return operation == LIFECYCLE_OPEN_HEAP || operation == LIFECYCLE_CLOSE_HEAP
               ? "heap"
               : "caller_owned";
}

static int validate_workspace(const spw_port_config_t* config) {
    spw_port_workspace_requirements_t requirements;
    if (spw_port_workspace_requirements(config, &requirements) != SPW_OK) {
        return 0;
    }
    return requirements.size <= sizeof(g_workspace.bytes) &&
           requirements.alignment <= _Alignof(lifecycle_workspace_t);
}

static int open_in_place_port(const spw_port_config_t* config,
                              spw_port_t** out_port) {
    return spw_port_open_in_place(config,
                                  g_workspace.bytes,
                                  sizeof(g_workspace.bytes),
                                  out_port) == SPW_OK;
}

static int sample_operation(lifecycle_operation_t operation,
                            const spw_port_config_t* config,
                            uint64_t* out_delta) {
    spw_port_t* port = NULL;
    uint64_t start;
    uint64_t end;
    spw_result_t result;

    switch (operation) {
    case LIFECYCLE_OPEN_HEAP:
        start = spw_profile_counter_read();
        result = spw_port_open(config, &port);
        end = spw_profile_counter_read();
        if (result != SPW_OK || port == NULL) {
            return 0;
        }
        if (spw_port_close(port) != SPW_OK) {
            return 0;
        }
        break;

    case LIFECYCLE_OPEN_IN_PLACE:
        start = spw_profile_counter_read();
        result = spw_port_open_in_place(config,
                                        g_workspace.bytes,
                                        sizeof(g_workspace.bytes),
                                        &port);
        end = spw_profile_counter_read();
        if (result != SPW_OK || port == NULL) {
            return 0;
        }
        if (spw_port_close(port) != SPW_OK) {
            return 0;
        }
        break;

    case LIFECYCLE_START:
        if (!open_in_place_port(config, &port)) {
            return 0;
        }
        start = spw_profile_counter_read();
        result = spw_port_start(port);
        end = spw_profile_counter_read();
        if (result != SPW_OK || spw_port_stop(port) != SPW_OK ||
            spw_port_close(port) != SPW_OK) {
            return 0;
        }
        break;

    case LIFECYCLE_STOP:
        if (!open_in_place_port(config, &port) ||
            spw_port_start(port) != SPW_OK) {
            return 0;
        }
        start = spw_profile_counter_read();
        result = spw_port_stop(port);
        end = spw_profile_counter_read();
        if (result != SPW_OK || spw_port_close(port) != SPW_OK) {
            return 0;
        }
        break;

    case LIFECYCLE_RESET:
        if (!open_in_place_port(config, &port) ||
            spw_port_start(port) != SPW_OK) {
            return 0;
        }
        start = spw_profile_counter_read();
        result = spw_port_reset(port);
        end = spw_profile_counter_read();
        if (result != SPW_OK || spw_port_close(port) != SPW_OK) {
            return 0;
        }
        break;

    case LIFECYCLE_CLOSE_IN_PLACE:
        if (!open_in_place_port(config, &port) ||
            spw_port_start(port) != SPW_OK ||
            spw_port_stop(port) != SPW_OK) {
            return 0;
        }
        start = spw_profile_counter_read();
        result = spw_port_close(port);
        end = spw_profile_counter_read();
        if (result != SPW_OK) {
            return 0;
        }
        break;

    case LIFECYCLE_CLOSE_HEAP:
        if (spw_port_open(config, &port) != SPW_OK || port == NULL ||
            spw_port_start(port) != SPW_OK ||
            spw_port_stop(port) != SPW_OK) {
            if (port != NULL) {
                (void)spw_port_close(port);
            }
            return 0;
        }
        start = spw_profile_counter_read();
        result = spw_port_close(port);
        end = spw_profile_counter_read();
        if (result != SPW_OK) {
            return 0;
        }
        break;

    default:
        return 0;
    }

    *out_delta = spw_profile_counter_delta(start, end);
    g_sink ^= *out_delta;
    return 1;
}

static void print_statistics(const lifecycle_statistics_t* statistics) {
    printf("{\"min\":%llu", (unsigned long long)statistics->minimum);
    printf(",\"median\":%.3f", statistics->median);
    printf(",\"mean\":%.3f", statistics->mean);
    printf(",\"p95\":%llu", (unsigned long long)statistics->p95);
    printf(",\"p99\":%llu", (unsigned long long)statistics->p99);
    printf(",\"max\":%llu", (unsigned long long)statistics->maximum);
    printf(",\"stddev\":%.3f}", statistics->standard_deviation);
}

static void print_result(lifecycle_operation_t operation,
                         size_t warmup_iterations,
                         size_t iterations,
                         const lifecycle_statistics_t* statistics) {
    printf("{\"schema\":\"spwkit.profile.lifecycle.v1\"");
    printf(",\"measurement_domain\":\"lifecycle\"");
    printf(",\"unit\":\"counter_ticks\"");
    printf(",\"backend\":\"loopback\"");
    printf(",\"operation\":\"%s\"", operation_name(operation));
    printf(",\"allocation\":\"%s\"", allocation_name(operation));
    printf(",\"boundary\":\"complete-public-api-call\"");
    printf(",\"fresh_port_per_sample\":true");
    printf(",\"counter_floor_subtracted\":false");
    printf(",\"warmup_iterations\":%zu", warmup_iterations);
    printf(",\"iterations\":%zu", iterations);
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
    fprintf(stderr, "Usage: %s [--warmup N] [--iterations N]\n", program);
}

int main(int argc, char** argv) {
    const spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
    size_t warmup_iterations = 256u;
    size_t iterations = 1024u;
    size_t arg;
    lifecycle_operation_t operation;

    for (arg = 1u; arg < (size_t)argc; ++arg) {
        if (strcmp(argv[arg], "--warmup") == 0 && arg + 1u < (size_t)argc) {
            if (!parse_size(argv[++arg], 0u, 1000000u, &warmup_iterations)) {
                print_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[arg], "--iterations") == 0 &&
                   arg + 1u < (size_t)argc) {
            if (!parse_size(argv[++arg], 1u, SPW_LIFECYCLE_MAX_SAMPLES,
                            &iterations)) {
                print_usage(argv[0]);
                return 2;
            }
        } else {
            print_usage(argv[0]);
            return 2;
        }
    }

    if (!validate_workspace(&config)) {
        fprintf(stderr, "LOOPBACK workspace exceeds lifecycle benchmark storage\n");
        return 1;
    }

    spw_profile_prepare();

    for (operation = LIFECYCLE_OPEN_HEAP;
         operation < LIFECYCLE_OPERATION_COUNT;
         operation = (lifecycle_operation_t)(operation + 1)) {
        lifecycle_statistics_t statistics;
        size_t i;
        uint64_t ignored;

        for (i = 0u; i < warmup_iterations; ++i) {
            if (!sample_operation(operation, &config, &ignored)) {
                fprintf(stderr, "lifecycle warmup failed for %s\n",
                        operation_name(operation));
                return 1;
            }
        }
        for (i = 0u; i < iterations; ++i) {
            if (!sample_operation(operation, &config, &g_samples[i])) {
                fprintf(stderr, "lifecycle sample failed for %s at %zu\n",
                        operation_name(operation), i);
                return 1;
            }
        }

        statistics = calculate_statistics(g_samples, iterations);
        print_result(operation, warmup_iterations, iterations, &statistics);
    }

    return g_sink == UINT64_MAX ? 1 : 0;
}
