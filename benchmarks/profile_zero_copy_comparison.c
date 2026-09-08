// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include "profiling/profile.h"

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPW_ZC_MAX_SAMPLES 4096u
#define SPW_ZC_MAX_PAYLOAD 4096u
#define SPW_ZC_WORKSPACE_SIZE 16384u
#define SPW_ZC_TOKEN 0x5a435458u

typedef struct zc_driver {
    spw_link_state_t state;
    uint8_t dma_storage[SPW_ZC_MAX_PAYLOAD];
    size_t length;
    spw_terminator_t terminator;
    int acquired;
    int submitted;
    uint64_t provider_boundary_tick;
    volatile uint64_t sink;
} zc_driver_t;

typedef union zc_workspace {
    max_align_t alignment;
    uint8_t bytes[SPW_ZC_WORKSPACE_SIZE];
} zc_workspace_t;

typedef struct statistics {
    uint64_t minimum;
    double median;
    double mean;
    uint64_t p95;
    uint64_t p99;
    uint64_t maximum;
    double standard_deviation;
} statistics_t;

typedef struct zc_sample {
    uint64_t total;
    uint64_t acquire;
    uint64_t submit;
    uint64_t reclaim;
    uint64_t release;
} zc_sample_t;

static uint64_t g_copied_samples[SPW_ZC_MAX_SAMPLES];
static uint64_t g_zero_copy_samples[SPW_ZC_MAX_SAMPLES];
static uint64_t g_acquire_samples[SPW_ZC_MAX_SAMPLES];
static uint64_t g_submit_samples[SPW_ZC_MAX_SAMPLES];
static uint64_t g_reclaim_samples[SPW_ZC_MAX_SAMPLES];
static uint64_t g_release_samples[SPW_ZC_MAX_SAMPLES];
static uint8_t g_source[SPW_ZC_MAX_PAYLOAD];
static zc_workspace_t g_workspace;

static uint64_t elapsed(uint64_t start, uint64_t end) {
    return spw_profile_counter_delta(start, end);
}

static void fill_payload(uint8_t* destination, size_t length, uint32_t seed) {
    size_t i;
    for (i = 0u; i < length; ++i) {
        destination[i] = (uint8_t)((i * 29u + seed * 17u + 0x5du) & 0xffu);
    }
}

static void fill_descriptor(zc_driver_t* driver,
                            spw_driver_buffer_t* out_buffer) {
    out_buffer->data = driver->dma_storage;
    out_buffer->length = driver->length;
    out_buffer->capacity = sizeof(driver->dma_storage);
    out_buffer->terminator = driver->terminator;
    out_buffer->token = SPW_ZC_TOKEN;
}

static spw_result_t driver_start(void* raw) {
    zc_driver_t* driver = (zc_driver_t*)raw;
    driver->state = SPW_LINK_RUN;
    return SPW_OK;
}

static spw_result_t driver_stop(void* raw) {
    zc_driver_t* driver = (zc_driver_t*)raw;
    driver->state = SPW_LINK_READY;
    return SPW_OK;
}

static spw_result_t driver_reset(void* raw) {
    zc_driver_t* driver = (zc_driver_t*)raw;
    driver->state = SPW_LINK_ERROR_RESET;
    driver->length = 0u;
    driver->terminator = SPW_TERMINATOR_EOP;
    driver->acquired = 0;
    driver->submitted = 0;
    driver->provider_boundary_tick = 0u;
    driver->sink = 0u;
    return SPW_OK;
}

static spw_result_t driver_get_link_state(const void* raw,
                                          spw_link_state_t* out_state) {
    const zc_driver_t* driver = (const zc_driver_t*)raw;
    if (out_state == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_state = driver->state;
    return SPW_OK;
}

static spw_result_t driver_get_capabilities(const void* raw,
                                            spw_capabilities_t* out_capabilities) {
    (void)raw;
    if (out_capabilities == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    memset(out_capabilities, 0, sizeof(*out_capabilities));
    out_capabilities->max_packet_size = SPW_ZC_MAX_PAYLOAD;
    out_capabilities->tx_queue_depth = 1u;
    out_capabilities->rx_queue_depth = 1u;
    out_capabilities->buffer_alignment = 1u;
    return SPW_OK;
}

static spw_result_t copied_send(void* raw,
                                const spw_packet_t* packet,
                                spw_timeout_us_t timeout_us) {
    zc_driver_t* driver = (zc_driver_t*)raw;
    (void)timeout_us;
    if (driver == NULL || packet == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (driver->state != SPW_LINK_RUN || driver->acquired || driver->submitted) {
        return SPW_ERR_INVALID_STATE;
    }
    if (packet->length > sizeof(driver->dma_storage) ||
        (packet->length != 0u && packet->data == NULL)) {
        return SPW_ERR_INVALID_PACKET;
    }

    /*
     * Reference copied path: the provider owns the DMA-like storage and must
     * explicitly copy the caller packet into that storage before native
     * submission. The timestamp immediately after the copy is the common
     * provider/native boundary used by both compared paths.
     */
    if (packet->length != 0u) {
        memcpy(driver->dma_storage, packet->data, packet->length);
    }
    driver->length = packet->length;
    driver->terminator = packet->terminator;
    driver->provider_boundary_tick = spw_profile_counter_read();

    if (packet->length != 0u) {
        driver->sink += driver->dma_storage[0u];
        driver->sink += driver->dma_storage[packet->length - 1u];
    } else {
        driver->sink += 1u;
    }
    return SPW_OK;
}

static spw_result_t receive_stub(void* raw,
                                 spw_packet_t* packet,
                                 spw_timeout_us_t timeout_us) {
    (void)raw;
    (void)packet;
    (void)timeout_us;
    return SPW_ERR_TIMEOUT;
}

static spw_result_t acquire_tx_buffer(void* raw,
                                      size_t min_capacity,
                                      spw_timeout_us_t timeout_us,
                                      spw_driver_buffer_t* out_buffer) {
    zc_driver_t* driver = (zc_driver_t*)raw;
    (void)timeout_us;
    if (driver == NULL || out_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (driver->state != SPW_LINK_RUN) {
        return SPW_ERR_INVALID_STATE;
    }
    if (min_capacity > sizeof(driver->dma_storage)) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }
    if (driver->acquired || driver->submitted) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }
    driver->length = 0u;
    driver->terminator = SPW_TERMINATOR_EOP;
    driver->acquired = 1;
    fill_descriptor(driver, out_buffer);
    return SPW_OK;
}

static spw_result_t submit_tx_buffer(void* raw,
                                     const spw_driver_buffer_t* buffer,
                                     spw_timeout_us_t timeout_us) {
    zc_driver_t* driver = (zc_driver_t*)raw;
    (void)timeout_us;
    if (driver == NULL || buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!driver->acquired || driver->submitted ||
        buffer->token != SPW_ZC_TOKEN || buffer->data != driver->dma_storage ||
        buffer->length > sizeof(driver->dma_storage)) {
        return SPW_ERR_INVALID_STATE;
    }

    driver->length = buffer->length;
    driver->terminator = buffer->terminator;

    /* Same provider/native handoff point as copied_send(), but no memcpy. */
    driver->provider_boundary_tick = spw_profile_counter_read();
    driver->acquired = 0;
    driver->submitted = 1;

    if (buffer->length != 0u) {
        driver->sink += driver->dma_storage[0u];
        driver->sink += driver->dma_storage[buffer->length - 1u];
    } else {
        driver->sink += 1u;
    }
    return SPW_OK;
}

static spw_result_t reclaim_tx_buffer(void* raw,
                                      spw_timeout_us_t timeout_us,
                                      spw_driver_buffer_t* out_buffer) {
    zc_driver_t* driver = (zc_driver_t*)raw;
    (void)timeout_us;
    if (driver == NULL || out_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!driver->submitted || driver->acquired) {
        return SPW_ERR_TIMEOUT;
    }
    driver->submitted = 0;
    driver->acquired = 1;
    fill_descriptor(driver, out_buffer);
    return SPW_OK;
}

static spw_result_t release_tx_buffer(void* raw,
                                      const spw_driver_buffer_t* buffer) {
    zc_driver_t* driver = (zc_driver_t*)raw;
    if (driver == NULL || buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!driver->acquired || driver->submitted ||
        buffer->token != SPW_ZC_TOKEN || buffer->data != driver->dma_storage) {
        return SPW_ERR_INVALID_STATE;
    }
    driver->acquired = 0;
    driver->length = 0u;
    driver->terminator = SPW_TERMINATOR_EOP;
    return SPW_OK;
}

static spw_result_t acquire_rx_buffer_stub(void* raw,
                                           spw_timeout_us_t timeout_us,
                                           spw_driver_buffer_t* out_buffer) {
    (void)raw;
    (void)timeout_us;
    (void)out_buffer;
    return SPW_ERR_TIMEOUT;
}

static spw_result_t release_rx_buffer_stub(void* raw,
                                           const spw_driver_buffer_t* buffer) {
    (void)raw;
    (void)buffer;
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
    .send = copied_send,
    .receive = receive_stub,
    .acquire_tx_buffer = acquire_tx_buffer,
    .submit_tx_buffer = submit_tx_buffer,
    .reclaim_tx_buffer = reclaim_tx_buffer,
    .release_tx_buffer = release_tx_buffer,
    .acquire_rx_buffer = acquire_rx_buffer_stub,
    .release_rx_buffer = release_rx_buffer_stub,
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

static int run_copied_sample(zc_driver_t* driver,
                             spw_port_t* port,
                             size_t payload_size,
                             uint32_t seed,
                             uint64_t* out_delta) {
    spw_packet_t packet;
    uint64_t start;
    if (driver == NULL || port == NULL || out_delta == NULL) {
        return 0;
    }
    packet.data = g_source;
    packet.length = payload_size;
    packet.capacity = payload_size;
    packet.terminator = SPW_TERMINATOR_EOP;

    start = spw_profile_counter_read();
    fill_payload(g_source, payload_size, seed);
    if (spw_port_send(port, &packet, SPW_TIMEOUT_IMMEDIATE) != SPW_OK) {
        return 0;
    }
    *out_delta = elapsed(start, driver->provider_boundary_tick);
    return 1;
}

static int run_zero_copy_sample(zc_driver_t* driver,
                                spw_port_t* port,
                                size_t payload_size,
                                uint32_t seed,
                                zc_sample_t* out_sample) {
    spw_buffer_t* buffer = NULL;
    spw_buffer_view_t view;
    uint64_t total_start;
    uint64_t operation_start;
    uint64_t operation_end;

    if (driver == NULL || port == NULL || out_sample == NULL) {
        return 0;
    }
    memset(out_sample, 0, sizeof(*out_sample));

    total_start = spw_profile_counter_read();
    operation_start = total_start;
    if (spw_port_acquire_tx_buffer(port,
                                   payload_size,
                                   SPW_TIMEOUT_IMMEDIATE,
                                   &buffer) != SPW_OK) {
        return 0;
    }
    operation_end = spw_profile_counter_read();
    out_sample->acquire = elapsed(operation_start, operation_end);

    if (spw_buffer_get_view(buffer, &view) != SPW_OK ||
        view.capacity < payload_size) {
        return 0;
    }
    fill_payload(view.data, payload_size, seed);
    if (spw_buffer_set_packet(buffer,
                              payload_size,
                              SPW_TERMINATOR_EOP) != SPW_OK) {
        return 0;
    }

    operation_start = spw_profile_counter_read();
    if (spw_port_submit_tx_buffer(port,
                                  &buffer,
                                  SPW_TIMEOUT_IMMEDIATE) != SPW_OK) {
        return 0;
    }
    operation_end = spw_profile_counter_read();
    out_sample->submit = elapsed(operation_start, operation_end);
    out_sample->total = elapsed(total_start, driver->provider_boundary_tick);
    if (buffer != NULL) {
        return 0;
    }

    operation_start = spw_profile_counter_read();
    if (spw_port_reclaim_tx_buffer(port,
                                   SPW_TIMEOUT_IMMEDIATE,
                                   &buffer) != SPW_OK) {
        return 0;
    }
    operation_end = spw_profile_counter_read();
    out_sample->reclaim = elapsed(operation_start, operation_end);

    operation_start = spw_profile_counter_read();
    if (spw_port_release_tx_buffer(port, &buffer) != SPW_OK) {
        return 0;
    }
    operation_end = spw_profile_counter_read();
    out_sample->release = elapsed(operation_start, operation_end);
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
                         const statistics_t* copied_statistics,
                         const statistics_t* zero_copy_statistics,
                         const statistics_t* acquire_statistics,
                         const statistics_t* submit_statistics,
                         const statistics_t* reclaim_statistics,
                         const statistics_t* release_statistics) {
    const double median_delta =
        zero_copy_statistics->median - copied_statistics->median;
    const double mean_delta = zero_copy_statistics->mean - copied_statistics->mean;
    const long long p95_delta = (long long)zero_copy_statistics->p95 -
                                (long long)copied_statistics->p95;
    const long long p99_delta = (long long)zero_copy_statistics->p99 -
                                (long long)copied_statistics->p99;

    printf("{\"schema\":\"spwkit.profile.zero-copy-comparison.v1\"");
    printf(",\"measurement_domain\":\"software\"");
    printf(",\"unit\":\"counter_ticks\"");
    printf(",\"backend\":\"driver\",\"direction\":\"tx\"");
    printf(",\"fixture\":\"provider-owned-dma-buffer-reference\"");
    printf(",\"boundary\":\"application-preparation-to-provider-native-boundary\"");
    printf(",\"copied_path\":\"caller-fill-plus-explicit-provider-copy\"");
    printf(",\"zero_copy_path\":\"acquire-direct-fill-submit\"");
    printf(",\"completion_cleanup\":\"reclaim-plus-release-outside-total\"");
    printf(",\"provider_storage_shared\":true");
    printf(",\"sync_hook_present\":false");
    printf(",\"sample_order\":\"alternating-per-iteration\"");
    printf(",\"counter_floor_subtracted\":false");
    printf(",\"payload_bytes\":%zu", payload_size);
    printf(",\"warmup_iterations\":%zu,\"iterations\":%zu",
           warmup_iterations, iterations);
    printf(",\"platform\":{\"architecture\":\"%s\"}", architecture_name());
    printf(",\"compiler\":{\"family\":\"%s\",\"major\":%u,\"minor\":%u}",
           compiler_name(), compiler_major(), compiler_minor());
    printf(",\"counter\":{\"kind\":\"%s\",\"width_bits\":%u,\"frequency_hz\":%llu}",
           spw_profile_counter_kind(),
           (unsigned int)spw_profile_counter_width_bits(),
           (unsigned long long)spw_profile_counter_frequency_hz());
    printf(",\"copied_statistics\":");
    print_statistics(copied_statistics);
    printf(",\"zero_copy_statistics\":");
    print_statistics(zero_copy_statistics);
    printf(",\"ownership_statistics\":{\"acquire\":");
    print_statistics(acquire_statistics);
    printf(",\"submit\":");
    print_statistics(submit_statistics);
    printf(",\"reclaim\":");
    print_statistics(reclaim_statistics);
    printf(",\"release\":");
    print_statistics(release_statistics);
    printf("}");
    printf(",\"effective_software_throughput\":{\"copied\":");
    print_throughput(payload_size, copied_statistics);
    printf(",\"zero_copy\":");
    print_throughput(payload_size, zero_copy_statistics);
    printf("}");
    printf(",\"delta\":{\"definition\":\"zero-copy-minus-copied\"");
    printf(",\"median_ticks\":%.3f,\"mean_ticks\":%.3f",
           median_delta, mean_delta);
    printf(",\"p95_ticks\":%lld,\"p99_ticks\":%lld", p95_delta, p99_delta);
    if (copied_statistics->median != 0.0) {
        printf(",\"median_percent\":%.3f",
               100.0 * median_delta / copied_statistics->median);
    } else {
        printf(",\"median_percent\":null");
    }
    if (copied_statistics->mean != 0.0) {
        printf(",\"mean_percent\":%.3f",
               100.0 * mean_delta / copied_statistics->mean);
    } else {
        printf(",\"mean_percent\":null");
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
    zc_driver_t driver;
    spw_driver_config_t driver_config;
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DRIVER);
    spw_port_workspace_requirements_t requirements;
    spw_port_t* port = NULL;
    statistics_t copied_statistics;
    statistics_t zero_copy_statistics;
    statistics_t acquire_statistics;
    statistics_t submit_statistics;
    statistics_t reclaim_statistics;
    statistics_t release_statistics;
    size_t i;

    for (i = 1u; i < (size_t)argc; ++i) {
        if (strcmp(argv[i], "--warmup") == 0 && i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, 1000000u, &warmup_iterations)) {
                print_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--iterations") == 0 &&
                   i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 1u, SPW_ZC_MAX_SAMPLES, &iterations)) {
                print_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--payload") == 0 &&
                   i + 1u < (size_t)argc) {
            if (!parse_size(argv[++i], 0u, SPW_ZC_MAX_PAYLOAD, &payload_size)) {
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
    driver.terminator = SPW_TERMINATOR_EOP;
    driver_config = (spw_driver_config_t)
        SPW_DRIVER_CONFIG_INITIALIZER(&DRIVER_OPS, &driver);
    driver_config.tx_buffer_slots = 1u;
    driver_config.rx_buffer_slots = 1u;
    config.backend_config = &driver_config;
    config.backend_config_size = sizeof(driver_config);

    if (spw_port_workspace_requirements(&config, &requirements) != SPW_OK ||
        requirements.size > sizeof(g_workspace.bytes) ||
        requirements.alignment > _Alignof(max_align_t)) {
        fprintf(stderr, "zero-copy comparison workspace is insufficient\n");
        return 1;
    }
    if (spw_port_open_in_place(&config,
                               g_workspace.bytes,
                               sizeof(g_workspace.bytes),
                               &port) != SPW_OK ||
        spw_port_start(port) != SPW_OK) {
        fprintf(stderr, "failed to start zero-copy comparison DRIVER port\n");
        return 1;
    }

    spw_profile_prepare();

    for (i = 0u; i < warmup_iterations; ++i) {
        uint64_t copied_delta = 0u;
        zc_sample_t zc_sample;
        const uint32_t seed = (uint32_t)(i + 1u);
        if ((i & 1u) == 0u) {
            if (!run_copied_sample(&driver, port, payload_size, seed, &copied_delta) ||
                !run_zero_copy_sample(&driver, port, payload_size, seed, &zc_sample)) {
                fprintf(stderr, "zero-copy comparison warmup failed\n");
                return 1;
            }
        } else {
            if (!run_zero_copy_sample(&driver, port, payload_size, seed, &zc_sample) ||
                !run_copied_sample(&driver, port, payload_size, seed, &copied_delta)) {
                fprintf(stderr, "zero-copy comparison warmup failed\n");
                return 1;
            }
        }
    }

    for (i = 0u; i < iterations; ++i) {
        zc_sample_t zc_sample;
        const uint32_t seed = (uint32_t)(warmup_iterations + i + 1u);
        if ((i & 1u) == 0u) {
            if (!run_copied_sample(&driver,
                                   port,
                                   payload_size,
                                   seed,
                                   &g_copied_samples[i]) ||
                !run_zero_copy_sample(&driver,
                                      port,
                                      payload_size,
                                      seed,
                                      &zc_sample)) {
                fprintf(stderr, "zero-copy comparison sample failed\n");
                return 1;
            }
        } else {
            if (!run_zero_copy_sample(&driver,
                                      port,
                                      payload_size,
                                      seed,
                                      &zc_sample) ||
                !run_copied_sample(&driver,
                                   port,
                                   payload_size,
                                   seed,
                                   &g_copied_samples[i])) {
                fprintf(stderr, "zero-copy comparison sample failed\n");
                return 1;
            }
        }
        g_zero_copy_samples[i] = zc_sample.total;
        g_acquire_samples[i] = zc_sample.acquire;
        g_submit_samples[i] = zc_sample.submit;
        g_reclaim_samples[i] = zc_sample.reclaim;
        g_release_samples[i] = zc_sample.release;
    }

    copied_statistics = calculate_statistics(g_copied_samples, iterations);
    zero_copy_statistics = calculate_statistics(g_zero_copy_samples, iterations);
    acquire_statistics = calculate_statistics(g_acquire_samples, iterations);
    submit_statistics = calculate_statistics(g_submit_samples, iterations);
    reclaim_statistics = calculate_statistics(g_reclaim_samples, iterations);
    release_statistics = calculate_statistics(g_release_samples, iterations);

    print_result(payload_size,
                 warmup_iterations,
                 iterations,
                 &copied_statistics,
                 &zero_copy_statistics,
                 &acquire_statistics,
                 &submit_statistics,
                 &reclaim_statistics,
                 &release_statistics);

    if (spw_port_stop(port) != SPW_OK || spw_port_close(port) != SPW_OK) {
        fprintf(stderr, "failed to close zero-copy comparison DRIVER port\n");
        return 1;
    }
    return driver.sink == UINT64_MAX ? 1 : 0;
}
