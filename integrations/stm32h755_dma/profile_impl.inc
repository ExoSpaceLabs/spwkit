/* SPDX-License-Identifier: Apache-2.0 */
/* Included by firmware.c only when SPWKIT_STM32_PROFILE is enabled. */

#define STM32_PROFILE_MAGIC UINT32_C(0x53575050) /* "SWPP" */
#define STM32_PROFILE_VERSION 1u
#define STM32_PROFILE_CORE_HZ UINT32_C(64000000)
#define STM32_PROFILE_PAYLOAD_COUNT 5u

#ifndef SPWKIT_STM32_PROFILE_WARMUP
#define SPWKIT_STM32_PROFILE_WARMUP 16u
#endif
#ifndef SPWKIT_STM32_PROFILE_ITERATIONS
#define SPWKIT_STM32_PROFILE_ITERATIONS 64u
#endif

#if SPWKIT_STM32_PROFILE_ITERATIONS > 256
#error "STM32 physical profiling stores at most 256 samples per row"
#endif

typedef struct stm32_profile_row {
    uint32_t payload_bytes;
    volatile uint32_t samples[SPWKIT_STM32_PROFILE_ITERATIONS];
} stm32_profile_row_t;

typedef struct stm32_profile_evidence {
    uint32_t magic;
    uint32_t version;
    volatile uint32_t phase;
    volatile uint32_t result;
    uint32_t case_id;
    uint32_t start_probe_id;
    uint32_t end_probe_id;
    uint32_t core_hz;
    uint32_t warmup_iterations;
    uint32_t measured_iterations;
    uint32_t row_count;
    volatile uint32_t floor_samples[SPWKIT_STM32_PROFILE_ITERATIONS];
    stm32_profile_row_t rows[STM32_PROFILE_PAYLOAD_COUNT];
    volatile uint32_t cache_valid;
    stm32_profile_row_t cache_clean_rows[STM32_PROFILE_PAYLOAD_COUNT];
    stm32_profile_row_t cache_invalidate_rows[STM32_PROFILE_PAYLOAD_COUNT];
} stm32_profile_evidence_t;

volatile stm32_profile_evidence_t g_stm32h755_spwkit_profile = {
    .magic = STM32_PROFILE_MAGIC,
    .version = STM32_PROFILE_VERSION,
    .phase = 0u,
    .result = UINT32_C(0xffffffff),
    .case_id = (uint32_t)SPWKIT_STM32_PROFILE_CASE,
    .start_probe_id = (uint32_t)SPWKIT_PROFILE_START,
    .end_probe_id = (uint32_t)SPWKIT_PROFILE_END,
    .core_hz = STM32_PROFILE_CORE_HZ,
    .warmup_iterations = (uint32_t)SPWKIT_STM32_PROFILE_WARMUP,
    .measured_iterations = (uint32_t)SPWKIT_STM32_PROFILE_ITERATIONS,
    .row_count = STM32_PROFILE_PAYLOAD_COUNT,
    .cache_valid = 0u
};

static uint8_t g_profile_tx_data[STM32_DMA_PACKET_CAPACITY];
static uint8_t g_profile_rx_data[STM32_DMA_PACKET_CAPACITY];
static volatile uint32_t g_profile_cache_sink;

static const uint32_t STM32_PROFILE_PAYLOADS[STM32_PROFILE_PAYLOAD_COUNT] = {
    0u, 1u, 8u, 64u, 256u
};

static void profile_fill(uint8_t* data, size_t length, uint8_t seed) {
    size_t index;
    for (index = 0u; index < length; ++index) {
        data[index] = (uint8_t)(seed + (uint8_t)index);
    }
}

static int profile_capture_delta(uint32_t* out_cycles) {
    const volatile spw_profile_sample_t* sample = spw_profile_last_sample();
    if (out_cycles == NULL || sample == NULL || sample->sequence != 1u ||
        sample->delta > UINT32_MAX) {
        return 0;
    }
    *out_cycles = (uint32_t)sample->delta;
    return 1;
}

static int profile_make_port(spw_port_t** out_port) {
    spw_driver_config_t driver_config =
        SPW_DRIVER_CONFIG_INITIALIZER(&STM32_DRIVER_OPS, &g_driver);
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DRIVER);
    spw_port_workspace_requirements_t requirements = {0u, 0u};

    if (out_port == NULL) {
        return 0;
    }
    driver_config.tx_buffer_slots = 1u;
    driver_config.rx_buffer_slots = 1u;
    config.backend_config = &driver_config;
    config.backend_config_size = sizeof(driver_config);

    if (spw_port_workspace_requirements(&config, &requirements) != SPW_OK ||
        requirements.size > sizeof(g_workspace) ||
        requirements.alignment > alignof(max_align_t)) {
        return 0;
    }
    if (spw_port_open_in_place(&config, g_workspace, sizeof(g_workspace), out_port) != SPW_OK ||
        *out_port == NULL || spw_port_start(*out_port) != SPW_OK) {
        return 0;
    }
    return 1;
}

static int __attribute__((unused)) profile_copied_once(spw_port_t* port,
                               size_t length,
                               int measure_rx,
                               uint32_t* out_cycles) {
    spw_packet_t tx = {
        g_profile_tx_data, length, length, SPW_TERMINATOR_EOP
    };
    spw_packet_t rx = {
        g_profile_rx_data, 0u, sizeof(g_profile_rx_data), SPW_TERMINATOR_EOP
    };

    spw_profile_reset();
    if (spw_port_send(port, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK) {
        return 0;
    }
    if (!measure_rx && !profile_capture_delta(out_cycles)) {
        return 0;
    }
    if (spw_port_receive(port, &rx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK ||
        rx.length != length) {
        return 0;
    }
    if (measure_rx && !profile_capture_delta(out_cycles)) {
        return 0;
    }
    return 1;
}

static int __attribute__((unused)) profile_prepare_zc_tx(spw_port_t* port,
                                 size_t length,
                                 spw_buffer_t** out_buffer) {
    spw_buffer_view_t view = {0};
    if (spw_port_acquire_tx_buffer(port, length, SPW_TIMEOUT_IMMEDIATE, out_buffer) != SPW_OK ||
        *out_buffer == NULL || spw_buffer_get_view(*out_buffer, &view) != SPW_OK ||
        view.capacity < length) {
        return 0;
    }
    profile_fill(view.data, length, 0x31u);
    return spw_buffer_set_packet(*out_buffer, length, SPW_TERMINATOR_EOP) == SPW_OK;
}

static int __attribute__((unused)) profile_release_rx_ready(spw_port_t* port) {
    spw_buffer_t* rx = NULL;
    if (spw_port_acquire_rx_buffer(port, SPW_TIMEOUT_IMMEDIATE, &rx) != SPW_OK ||
        rx == NULL) {
        return 0;
    }
    return spw_port_release_rx_buffer(port, &rx) == SPW_OK && rx == NULL;
}

static int __attribute__((unused)) profile_reclaim_release_tx(spw_port_t* port) {
    spw_buffer_t* reclaimed = NULL;
    if (spw_port_reclaim_tx_buffer(port, SPW_TIMEOUT_IMMEDIATE, &reclaimed) != SPW_OK ||
        reclaimed == NULL) {
        return 0;
    }
    return spw_port_release_tx_buffer(port, &reclaimed) == SPW_OK && reclaimed == NULL;
}

static int __attribute__((unused)) profile_zc_once(spw_port_t* port, size_t length, uint32_t* out_cycles) {
    spw_buffer_t* tx = NULL;
    spw_buffer_t* reclaimed = NULL;
    spw_buffer_t* rx = NULL;
    (void)tx;
    (void)reclaimed;
    (void)rx;

#if SPWKIT_STM32_PROFILE_CASE == 3
    spw_profile_reset();
    if (spw_port_acquire_tx_buffer(port, length, SPW_TIMEOUT_IMMEDIATE, &tx) != SPW_OK ||
        tx == NULL || !profile_capture_delta(out_cycles)) {
        return 0;
    }
    return spw_port_release_tx_buffer(port, &tx) == SPW_OK && tx == NULL;

#elif SPWKIT_STM32_PROFILE_CASE == 4
    if (!profile_prepare_zc_tx(port, length, &tx)) {
        return 0;
    }
    spw_profile_reset();
    if (spw_port_submit_tx_buffer(port, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK ||
        tx != NULL || !profile_capture_delta(out_cycles)) {
        return 0;
    }
    return profile_release_rx_ready(port) && profile_reclaim_release_tx(port);

#elif SPWKIT_STM32_PROFILE_CASE == 5
    if (!profile_prepare_zc_tx(port, length, &tx)) {
        return 0;
    }
    /* Zero-length submit completes synchronously, so its provider completion
     * boundary must remain inside the selected measurement interval. */
    if (length == 0u) {
        spw_profile_reset();
    }
    if (spw_port_submit_tx_buffer(port, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK || tx != NULL) {
        return 0;
    }
    if (length != 0u) {
        spw_profile_reset();
    }
    if (spw_port_reclaim_tx_buffer(port, SPW_TIMEOUT_IMMEDIATE, &reclaimed) != SPW_OK ||
        reclaimed == NULL || !profile_capture_delta(out_cycles)) {
        return 0;
    }
    if (!profile_release_rx_ready(port)) {
        return 0;
    }
    return spw_port_release_tx_buffer(port, &reclaimed) == SPW_OK && reclaimed == NULL;

#elif SPWKIT_STM32_PROFILE_CASE == 6
    if (!profile_prepare_zc_tx(port, length, &tx) ||
        spw_port_submit_tx_buffer(port, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK || tx != NULL ||
        spw_port_reclaim_tx_buffer(port, SPW_TIMEOUT_IMMEDIATE, &reclaimed) != SPW_OK ||
        reclaimed == NULL || !profile_release_rx_ready(port)) {
        return 0;
    }
    spw_profile_reset();
    if (spw_port_release_tx_buffer(port, &reclaimed) != SPW_OK || reclaimed != NULL ||
        !profile_capture_delta(out_cycles)) {
        return 0;
    }
    return 1;

#elif SPWKIT_STM32_PROFILE_CASE == 7
    if (!profile_prepare_zc_tx(port, length, &tx)) {
        return 0;
    }
    /* As above, zero-length provider data-ready is emitted during submit. */
    if (length == 0u) {
        spw_profile_reset();
    }
    if (spw_port_submit_tx_buffer(port, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK || tx != NULL) {
        return 0;
    }
    if (length != 0u) {
        spw_profile_reset();
    }
    if (spw_port_acquire_rx_buffer(port, SPW_TIMEOUT_IMMEDIATE, &rx) != SPW_OK ||
        rx == NULL || !profile_capture_delta(out_cycles)) {
        return 0;
    }
    if (spw_port_release_rx_buffer(port, &rx) != SPW_OK || rx != NULL) {
        return 0;
    }
    return profile_reclaim_release_tx(port);

#elif SPWKIT_STM32_PROFILE_CASE == 8
    if (!profile_prepare_zc_tx(port, length, &tx) ||
        spw_port_submit_tx_buffer(port, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK || tx != NULL ||
        spw_port_acquire_rx_buffer(port, SPW_TIMEOUT_IMMEDIATE, &rx) != SPW_OK || rx == NULL ||
        spw_port_reclaim_tx_buffer(port, SPW_TIMEOUT_IMMEDIATE, &reclaimed) != SPW_OK ||
        reclaimed == NULL) {
        return 0;
    }
    spw_profile_reset();
    if (spw_port_release_rx_buffer(port, &rx) != SPW_OK || rx != NULL ||
        !profile_capture_delta(out_cycles)) {
        return 0;
    }
    return spw_port_release_tx_buffer(port, &reclaimed) == SPW_OK && reclaimed == NULL;
#else
    (void)port;
    (void)length;
    (void)out_cycles;
    return 0;
#endif
}

static int profile_operation_once(spw_port_t* port,
                                  size_t length,
                                  uint32_t* out_cycles) {
#if SPWKIT_STM32_PROFILE_CASE == 1
    return profile_copied_once(port, length, 0, out_cycles);
#elif SPWKIT_STM32_PROFILE_CASE == 2
    return profile_copied_once(port, length, 1, out_cycles);
#else
    return profile_zc_once(port, length, out_cycles);
#endif
}

static void profile_measure_floor(void) {
    uint32_t index;
    for (index = 0u; index < (uint32_t)SPWKIT_STM32_PROFILE_WARMUP; ++index) {
        const uint64_t start = spw_profile_counter_read();
        const uint64_t end = spw_profile_counter_read();
        g_profile_cache_sink ^= (uint32_t)spw_profile_counter_delta(start, end);
    }
    for (index = 0u; index < (uint32_t)SPWKIT_STM32_PROFILE_ITERATIONS; ++index) {
        const uint64_t start = spw_profile_counter_read();
        const uint64_t end = spw_profile_counter_read();
        g_stm32h755_spwkit_profile.floor_samples[index] =
            (uint32_t)spw_profile_counter_delta(start, end);
    }
}

static uint32_t profile_time_cache_clean(size_t length) {
    const uint64_t start = spw_profile_counter_read();
    clean_cache(g_dma_tx, length);
    return (uint32_t)spw_profile_counter_delta(start, spw_profile_counter_read());
}

static uint32_t profile_time_cache_invalidate(size_t length) {
    const uint64_t start = spw_profile_counter_read();
    invalidate_cache(g_dma_rx, length);
    return (uint32_t)spw_profile_counter_delta(start, spw_profile_counter_read());
}

static void profile_measure_cache_rows(void) {
    uint32_t row;
    uint32_t index;

    g_stm32h755_spwkit_profile.cache_valid = 1u;
    for (row = 0u; row < STM32_PROFILE_PAYLOAD_COUNT; ++row) {
        const size_t length = (size_t)STM32_PROFILE_PAYLOADS[row];
        g_stm32h755_spwkit_profile.cache_clean_rows[row].payload_bytes = (uint32_t)length;
        g_stm32h755_spwkit_profile.cache_invalidate_rows[row].payload_bytes = (uint32_t)length;

        for (index = 0u; index < (uint32_t)SPWKIT_STM32_PROFILE_WARMUP; ++index) {
            profile_fill(g_dma_tx, length, (uint8_t)index);
            g_profile_cache_sink ^= profile_time_cache_clean(length);
            profile_fill(g_dma_rx, length, (uint8_t)(index + 1u));
            clean_cache(g_dma_rx, length);
            g_profile_cache_sink ^= profile_time_cache_invalidate(length);
        }

        for (index = 0u; index < (uint32_t)SPWKIT_STM32_PROFILE_ITERATIONS; ++index) {
            profile_fill(g_dma_tx, length, (uint8_t)(index + 3u));
            g_stm32h755_spwkit_profile.cache_clean_rows[row].samples[index] =
                profile_time_cache_clean(length);

            profile_fill(g_dma_rx, length, (uint8_t)(index + 7u));
            clean_cache(g_dma_rx, length);
            g_stm32h755_spwkit_profile.cache_invalidate_rows[row].samples[index] =
                profile_time_cache_invalidate(length);
        }
    }
}

static int run_profile(void) {
    spw_port_t* port = NULL;
    uint32_t row;
    uint32_t index;
    uint32_t cycles = 0u;

    g_stm32h755_spwkit_profile.phase = 1u;
    spw_profile_prepare();
    profile_measure_floor();

    if (!profile_make_port(&port)) {
        return 0x801;
    }

    for (row = 0u; row < STM32_PROFILE_PAYLOAD_COUNT; ++row) {
        const size_t length = (size_t)STM32_PROFILE_PAYLOADS[row];
        g_stm32h755_spwkit_profile.phase = 0x100u + row;
        g_stm32h755_spwkit_profile.rows[row].payload_bytes = (uint32_t)length;
        profile_fill(g_profile_tx_data, length, (uint8_t)(0x40u + row));

        for (index = 0u; index < (uint32_t)SPWKIT_STM32_PROFILE_WARMUP; ++index) {
            if (!profile_operation_once(port, length, &cycles)) {
                return (int)(0x900u + row);
            }
        }
        for (index = 0u; index < (uint32_t)SPWKIT_STM32_PROFILE_ITERATIONS; ++index) {
            if (!profile_operation_once(port, length, &cycles)) {
                return (int)(0xa00u + row);
            }
            g_stm32h755_spwkit_profile.rows[row].samples[index] = cycles;
        }
    }

    if ((uint32_t)SPWKIT_STM32_PROFILE_CASE == 1u) {
        g_stm32h755_spwkit_profile.phase = 0x600u;
        profile_measure_cache_rows();
    }

    if (spw_port_close(port) != SPW_OK) {
        return 0xb01;
    }
    return 0;
}
