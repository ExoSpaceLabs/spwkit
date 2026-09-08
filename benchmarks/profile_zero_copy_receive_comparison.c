// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include "profiling/profile.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPW_ZC_RX_MAX_SAMPLES 4096u
#define SPW_ZC_RX_MAX_PAYLOAD 4096u
#define SPW_ZC_RX_WORKSPACE_SIZE 16384u
#define SPW_ZC_RX_TOKEN 0x5a435258u

typedef struct rx_driver {
    spw_link_state_t state;
    uint8_t dma_storage[SPW_ZC_RX_MAX_PAYLOAD];
    size_t payload_length;
    int rx_acquired;
    uint64_t data_ready_tick;
    volatile uint64_t sink;
} rx_driver_t;

typedef union rx_workspace {
    max_align_t alignment;
    uint8_t bytes[SPW_ZC_RX_WORKSPACE_SIZE];
} rx_workspace_t;

typedef struct statistics {
    uint64_t minimum;
    double median;
    double mean;
    uint64_t p95;
    uint64_t p99;
    uint64_t maximum;
    double standard_deviation;
} statistics_t;

typedef struct zero_copy_rx_sample {
    uint64_t visibility;
    uint64_t acquire_call;
    uint64_t release;
} zero_copy_rx_sample_t;

static uint64_t g_copied_visibility_samples[SPW_ZC_RX_MAX_SAMPLES];
static uint64_t g_zero_copy_visibility_samples[SPW_ZC_RX_MAX_SAMPLES];
static uint64_t g_copied_api_samples[SPW_ZC_RX_MAX_SAMPLES];
static uint64_t g_acquire_samples[SPW_ZC_RX_MAX_SAMPLES];
static uint64_t g_release_samples[SPW_ZC_RX_MAX_SAMPLES];
static uint8_t g_copied_destination[SPW_ZC_RX_MAX_PAYLOAD];
static rx_workspace_t g_workspace;

static uint64_t elapsed(uint64_t start, uint64_t end) {
    return spw_profile_counter_delta(start, end);
}

static void prepare_payload(rx_driver_t* driver, size_t length, uint32_t seed) {
    size_t i;
    driver->payload_length = length;
    for (i = 0u; i < length; ++i) {
        driver->dma_storage[i] =
            (uint8_t)((i * 31u + seed * 19u + 0x47u) & 0xffu);
    }
}

static void fill_rx_descriptor(rx_driver_t* driver,
                               spw_driver_buffer_t* out_buffer) {
    out_buffer->data = driver->dma_storage;
    out_buffer->length = driver->payload_length;
    out_buffer->capacity = sizeof(driver->dma_storage);
    out_buffer->terminator = SPW_TERMINATOR_EOP;
    out_buffer->token = SPW_ZC_RX_TOKEN;
}

static spw_result_t driver_start(void* raw) {
    ((rx_driver_t*)raw)->state = SPW_LINK_RUN;
    return SPW_OK;
}

static spw_result_t driver_stop(void* raw) {
    ((rx_driver_t*)raw)->state = SPW_LINK_READY;
    return SPW_OK;
}

static spw_result_t driver_reset(void* raw) {
    rx_driver_t* driver = (rx_driver_t*)raw;
    driver->state = SPW_LINK_ERROR_RESET;
    driver->payload_length = 0u;
    driver->rx_acquired = 0;
    driver->data_ready_tick = 0u;
    driver->sink = 0u;
    return SPW_OK;
}

static spw_result_t driver_get_link_state(const void* raw,
                                          spw_link_state_t* out_state) {
    if (raw == NULL || out_state == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_state = ((const rx_driver_t*)raw)->state;
    return SPW_OK;
}

static spw_result_t driver_get_capabilities(const void* raw,
                                            spw_capabilities_t* out_capabilities) {
    (void)raw;
    if (out_capabilities == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    memset(out_capabilities, 0, sizeof(*out_capabilities));
    out_capabilities->max_packet_size = SPW_ZC_RX_MAX_PAYLOAD;
    out_capabilities->tx_queue_depth = 1u;
    out_capabilities->rx_queue_depth = 1u;
    out_capabilities->buffer_alignment = 1u;
    return SPW_OK;
}

static spw_result_t send_stub(void* raw,
                              const spw_packet_t* packet,
                              spw_timeout_us_t timeout_us) {
    (void)raw;
    (void)packet;
    (void)timeout_us;
    return SPW_ERR_UNSUPPORTED;
}

static spw_result_t copied_receive(void* raw,
                                   spw_packet_t* packet,
                                   spw_timeout_us_t timeout_us) {
    rx_driver_t* driver = (rx_driver_t*)raw;
    (void)timeout_us;
    if (driver == NULL || packet == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (driver->state != SPW_LINK_RUN || driver->rx_acquired) {
        return SPW_ERR_INVALID_STATE;
    }
    if (packet->capacity < driver->payload_length ||
        (driver->payload_length != 0u && packet->data == NULL)) {
        packet->length = driver->payload_length;
        return SPW_ERR_BUFFER_TOO_SMALL;
    }

    /* Common provider/native data-ready boundary for copied and zero-copy RX. */
    driver->data_ready_tick = spw_profile_counter_read();
    if (driver->payload_length != 0u) {
        memcpy(packet->data, driver->dma_storage, driver->payload_length);
        driver->sink += packet->data[0u];
        driver->sink += packet->data[driver->payload_length - 1u];
    } else {
        driver->sink += 1u;
    }
    packet->length = driver->payload_length;
    packet->terminator = SPW_TERMINATOR_EOP;
    return SPW_OK;
}

static spw_result_t tx_acquire_stub(void* raw,
                                    size_t min_capacity,
                                    spw_timeout_us_t timeout_us,
                                    spw_driver_buffer_t* out_buffer) {
    (void)raw;
    (void)min_capacity;
    (void)timeout_us;
    (void)out_buffer;
    return SPW_ERR_UNSUPPORTED;
}

static spw_result_t tx_submit_stub(void* raw,
                                   const spw_driver_buffer_t* buffer,
                                   spw_timeout_us_t timeout_us) {
    (void)raw;
    (void)buffer;
    (void)timeout_us;
    return SPW_ERR_UNSUPPORTED;
}

static spw_result_t tx_reclaim_stub(void* raw,
                                    spw_timeout_us_t timeout_us,
                                    spw_driver_buffer_t* out_buffer) {
    (void)raw;
    (void)timeout_us;
    (void)out_buffer;
    return SPW_ERR_UNSUPPORTED;
}

static spw_result_t tx_release_stub(void* raw,
                                    const spw_driver_buffer_t* buffer) {
    (void)raw;
    (void)buffer;
    return SPW_ERR_UNSUPPORTED;
}

static spw_result_t acquire_rx_buffer(void* raw,
                                      spw_timeout_us_t timeout_us,
                                      spw_driver_buffer_t* out_buffer) {
    rx_driver_t* driver = (rx_driver_t*)raw;
    (void)timeout_us;
    if (driver == NULL || out_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (driver->state != SPW_LINK_RUN) {
        return SPW_ERR_INVALID_STATE;
    }
    if (driver->rx_acquired) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    /* Same data-ready point as copied_receive(), before ownership plumbing. */
    driver->data_ready_tick = spw_profile_counter_read();
    fill_rx_descriptor(driver, out_buffer);
    driver->rx_acquired = 1;
    return SPW_OK;
}

static spw_result_t release_rx_buffer(void* raw,
                                      const spw_driver_buffer_t* buffer) {
    rx_driver_t* driver = (rx_driver_t*)raw;
    if (driver == NULL || buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!driver->rx_acquired || buffer->token != SPW_ZC_RX_TOKEN ||
        buffer->data != driver->dma_storage) {
        return SPW_ERR_INVALID_STATE;
    }
    driver->rx_acquired = 0;
    return SPW_OK;
}

static const spw_driver_ops_t DRIVER_OPS = {
    .struct_size = sizeof(spw_driver_ops_t),
    .version = SPW_DRIVER_OPS_VERSION,
    .start = driver_start,
    .stop = driver_stop,
    .reset = driver_reset,
    .get_link_state = driver_get_link_state,
    .get_capabilities = driver_get_capabilities,
    .send = send_stub,
    .receive = copied_receive,
    .acquire_tx_buffer = tx_acquire_stub,
    .submit_tx_buffer = tx_submit_stub,
    .reclaim_tx_buffer = tx_reclaim_stub,
    .release_tx_buffer = tx_release_stub,
    .acquire_rx_buffer = acquire_rx_buffer,
    .release_rx_buffer = release_rx_buffer,
    .sync_buffer = NULL,
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

static statistics_t calculate_statistics(uint64_t* samples, size_t count) {
    statistics_t statistics;
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

static int run_copied_sample(rx_driver_t* driver,
                             spw_port_t* port,
                             size_t payload_size,
                             uint32_t seed,
                             uint64_t* out_visibility,
                             uint64_t* out_api_call) {
    spw_packet_t packet;
    uint64_t api_start;
    uint64_t api_end;
    if (driver == NULL || port == NULL || out_visibility == NULL ||
        out_api_call == NULL) {
        return 0;
    }
    prepare_payload(driver, payload_size, seed);
    packet.data = g_copied_destination;
    packet.length = 0u;
    packet.capacity = sizeof(g_copied_destination);
    packet.terminator = SPW_TERMINATOR_EOP;

    api_start = spw_profile_counter_read();
    if (spw_port_receive(port, &packet, SPW_TIMEOUT_IMMEDIATE) != SPW_OK) {
        return 0;
    }
    api_end = spw_profile_counter_read();
    if (packet.length != payload_size) {
        return 0;
    }
    *out_visibility = elapsed(driver->data_ready_tick, api_end);
    *out_api_call = elapsed(api_start, api_end);
    return 1;
}

static int run_zero_copy_sample(rx_driver_t* driver,
                                spw_port_t* port,
                                size_t payload_size,
                                uint32_t seed,
                                zero_copy_rx_sample_t* out_sample) {
    spw_buffer_t* buffer = NULL;
    spw_buffer_view_t view;
    uint64_t api_start;
    uint64_t api_end;
    uint64_t release_start;
    uint64_t release_end;
    if (driver == NULL || port == NULL || out_sample == NULL) {
        return 0;
    }
    memset(out_sample, 0, sizeof(*out_sample));
    prepare_payload(driver, payload_size, seed);

    api_start = spw_profile_counter_read();
    if (spw_port_acquire_rx_buffer(port,
                                   SPW_TIMEOUT_IMMEDIATE,
                                   &buffer) != SPW_OK) {
        return 0;
    }
    api_end = spw_profile_counter_read();
    out_sample->visibility = elapsed(driver->data_ready_tick, api_end);
    out_sample->acquire_call = elapsed(api_start, api_end);

    if (spw_buffer_get_view(buffer, &view) != SPW_OK ||
        view.length != payload_size || view.data != driver->dma_storage) {
        return 0;
    }
    if (payload_size != 0u) {
        driver->sink += view.data[0u];
        driver->sink += view.data[payload_size - 1u];
    } else {
        driver->sink += 1u;
    }

    release_start = spw_profile_counter_read();
    if (spw_port_release_rx_buffer(port, &buffer) != SPW_OK) {
        return 0;
    }
    release_end = spw_profile_counter_read();
    out_sample->release = elapsed(release_start, release_end);
    return buffer == NULL;
}

static void print_statistics(const statistics_t* statistics) {
    printf("{\"min\":%llu", (unsigned long long)statistics->minimum);
    printf(",\"median\":%.3f", statistics->median);
    printf(",\"mean\":%.3f", statistics->mean);
    printf(",\"p95\":%llu", (unsigned long long)statistics->p95);
    printf(",\"p99\":%llu", (unsigned long long)statistics->p99);
    printf(",\"max\":%llu", (unsigned long long)statistics->maximum);
    printf(",\"stddev\":%.3f}", statistics->standard_deviation);
}

static void print_throughput(size_t payload_size,
                             const statistics_t* statistics) {
    const uint64_t frequency_hz = spw_profile_counter_frequency_hz();
    if (payload_size == 0u || statistics->median <= 0.0) {
        printf("null");
        return;
    }
    printf("{\"bytes_per_counter_tick\":%.9f",
           (double)payload_size / statistics->median);
    if (frequency_hz != 0u) {
        const double bytes_per_second =
            (double)payload_size * (double)frequency_hz / statistics->median;
        printf(",\"bytes_per_second\":%.3f", bytes_per_second);
        printf(",\"megabits_per_second\":%.6f",
               bytes_per_second * 8.0 / 1000000.0);
    } else {
        printf(",\"bytes_per_second\":null,\"megabits_per_second\":null");
    }
    printf("}");
}

static void print_result(size_t payload_size,
                         size_t warmup_iterations,
                         size_t iterations,
                         const statistics_t* copied_visibility,
                         const statistics_t* zero_copy_visibility,
                         const statistics_t* copied_api,
                         const statistics_t* acquire,
                         const statistics_t* release) {
    const double median_delta = zero_copy_visibility->median - copied_visibility->median;
    const double mean_delta = zero_copy_visibility->mean - copied_visibility->mean;
    const long long p95_delta = (long long)zero_copy_visibility->p95 -
                                (long long)copied_visibility->p95;
    const long long p99_delta = (long long)zero_copy_visibility->p99 -
                                (long long)copied_visibility->p99;

    printf("{\"schema\":\"spwkit.profile.zero-copy-rx-comparison.v1\"");
    printf(",\"measurement_domain\":\"software\",\"unit\":\"counter_ticks\"");
    printf(",\"backend\":\"driver\",\"direction\":\"rx\"");
    printf(",\"fixture\":\"provider-owned-dma-buffer-reference\"");
    printf(",\"boundary\":\"provider-data-ready-to-application-visibility\"");
    printf(",\"copied_path\":\"provider-storage-to-caller-buffer-copy\"");
    printf(",\"zero_copy_path\":\"acquire-provider-storage-directly\"");
    printf(",\"release_outside_visibility_interval\":true");
    printf(",\"provider_storage_shared\":true,\"sync_hook_present\":false");
    printf(",\"sample_order\":\"alternating-per-iteration\"");
    printf(",\"counter_floor_subtracted\":false");
    printf(",\"payload_bytes\":%zu,\"warmup_iterations\":%zu,\"iterations\":%zu",
           payload_size, warmup_iterations, iterations);
    printf(",\"platform\":{\"architecture\":\"%s\"}", architecture_name());
    printf(",\"compiler\":{\"family\":\"%s\",\"major\":%u,\"minor\":%u}",
           compiler_name(), compiler_major(), compiler_minor());
    printf(",\"counter\":{\"kind\":\"%s\",\"width_bits\":%u,\"frequency_hz\":%llu}",
           spw_profile_counter_kind(),
           (unsigned int)spw_profile_counter_width_bits(),
           (unsigned long long)spw_profile_counter_frequency_hz());
    printf(",\"copied_visibility_statistics\":");
    print_statistics(copied_visibility);
    printf(",\"zero_copy_visibility_statistics\":");
    print_statistics(zero_copy_visibility);
    printf(",\"ownership_statistics\":{\"copied_api_call\":");
    print_statistics(copied_api);
    printf(",\"acquire\":");
    print_statistics(acquire);
    printf(",\"release\":");
    print_statistics(release);
    printf("}");
    printf(",\"effective_software_throughput\":{\"copied\":");
    print_throughput(payload_size, copied_visibility);
    printf(",\"zero_copy\":");
    print_throughput(payload_size, zero_copy_visibility);
    printf("}");
    printf(",\"delta\":{\"definition\":\"zero-copy-minus-copied\"");
    printf(",\"median_ticks\":%.3f,\"mean_ticks\":%.3f",
           median_delta, mean_delta);
    printf(",\"p95_ticks\":%lld,\"p99_ticks\":%lld", p95_delta, p99_delta);
    if (copied_visibility->median != 0.0) {
        printf(",\"median_percent\":%.3f",
               100.0 * median_delta / copied_visibility->median);
    } else {
        printf(",\"median_percent\":null");
    }
    printf(",\"zero_copy_faster_by_median\":%s}",
           median_delta < 0.0 ? "true" : "false");
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
    rx_driver_t driver;
    spw_driver_config_t driver_config;
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DRIVER);
    spw_port_workspace_requirements_t requirements;
    spw_port_t* port = NULL;
    statistics_t copied_visibility;
    statistics_t zero_copy_visibility;
    statistics_t copied_api;
    statistics_t acquire;
    statistics_t release;
    size_t i;

    for (i = 1u; i < (size_t)argc; ++i) {
        if (strcmp(argv[i], "--warmup") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, 1000000u, &warmup_iterations)) {
                print_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--iterations") == 0 &&
                   i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 1u, SPW_ZC_RX_MAX_SAMPLES, &iterations)) {
                print_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--payload") == 0 &&
                   i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, SPW_ZC_RX_MAX_PAYLOAD, &payload_size)) {
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
        SPW_DRIVER_CONFIG_INITIALIZER(&DRIVER_OPS, &driver);
    driver_config.tx_buffer_slots = 1u;
    driver_config.rx_buffer_slots = 1u;
    config.backend_config = &driver_config;
    config.backend_config_size = sizeof(driver_config);

    if (spw_port_workspace_requirements(&config, &requirements) != SPW_OK ||
        requirements.size > sizeof(g_workspace.bytes) ||
        requirements.alignment > _Alignof(max_align_t)) {
        fprintf(stderr, "zero-copy RX comparison workspace is insufficient\n");
        return 1;
    }
    if (spw_port_open_in_place(&config,
                               g_workspace.bytes,
                               sizeof(g_workspace.bytes),
                               &port) != SPW_OK ||
        spw_port_start(port) != SPW_OK) {
        fprintf(stderr, "failed to start zero-copy RX comparison DRIVER port\n");
        return 1;
    }

    spw_profile_prepare();

    for (i = 0u; i < warmup_iterations; ++i) {
        uint64_t copied_visibility_sample = 0u;
        uint64_t copied_api_sample = 0u;
        zero_copy_rx_sample_t zc_sample;
        const uint32_t seed = (uint32_t)(i + 1u);
        if ((i & 1u) == 0u) {
            if (!run_copied_sample(&driver, port, payload_size, seed,
                                   &copied_visibility_sample, &copied_api_sample) ||
                !run_zero_copy_sample(&driver, port, payload_size, seed, &zc_sample)) {
                fprintf(stderr, "zero-copy RX warmup failed\n");
                return 1;
            }
        } else {
            if (!run_zero_copy_sample(&driver, port, payload_size, seed, &zc_sample) ||
                !run_copied_sample(&driver, port, payload_size, seed,
                                   &copied_visibility_sample, &copied_api_sample)) {
                fprintf(stderr, "zero-copy RX warmup failed\n");
                return 1;
            }
        }
    }

    for (i = 0u; i < iterations; ++i) {
        zero_copy_rx_sample_t zc_sample;
        const uint32_t seed = (uint32_t)(warmup_iterations + i + 1u);
        if ((i & 1u) == 0u) {
            if (!run_copied_sample(&driver, port, payload_size, seed,
                                   &g_copied_visibility_samples[i],
                                   &g_copied_api_samples[i]) ||
                !run_zero_copy_sample(&driver, port, payload_size, seed, &zc_sample)) {
                fprintf(stderr, "zero-copy RX sample failed\n");
                return 1;
            }
        } else {
            if (!run_zero_copy_sample(&driver, port, payload_size, seed, &zc_sample) ||
                !run_copied_sample(&driver, port, payload_size, seed,
                                   &g_copied_visibility_samples[i],
                                   &g_copied_api_samples[i])) {
                fprintf(stderr, "zero-copy RX sample failed\n");
                return 1;
            }
        }
        g_zero_copy_visibility_samples[i] = zc_sample.visibility;
        g_acquire_samples[i] = zc_sample.acquire_call;
        g_release_samples[i] = zc_sample.release;
    }

    copied_visibility = calculate_statistics(g_copied_visibility_samples, iterations);
    zero_copy_visibility = calculate_statistics(g_zero_copy_visibility_samples, iterations);
    copied_api = calculate_statistics(g_copied_api_samples, iterations);
    acquire = calculate_statistics(g_acquire_samples, iterations);
    release = calculate_statistics(g_release_samples, iterations);

    print_result(payload_size,
                 warmup_iterations,
                 iterations,
                 &copied_visibility,
                 &zero_copy_visibility,
                 &copied_api,
                 &acquire,
                 &release);

    if (spw_port_stop(port) != SPW_OK || spw_port_close(port) != SPW_OK) {
        fprintf(stderr, "failed to close zero-copy RX comparison DRIVER port\n");
        return 1;
    }
    return driver.sink == UINT64_MAX ? 1 : 0;
}
