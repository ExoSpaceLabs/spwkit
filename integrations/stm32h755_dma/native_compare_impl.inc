/* SPDX-License-Identifier: Apache-2.0 */
/* Included by profile_impl.inc for physical native-vs-SpWKit cases 9..12. */

#define STM32_PROFILE_MAGIC UINT32_C(0x53575050) /* "SWPP" */
#define STM32_PROFILE_VERSION 2u
#define STM32_PROFILE_CORE_HZ UINT32_C(64000000)
#define STM32_PROFILE_PAYLOAD_COUNT 5u

#ifndef SPWKIT_STM32_PROFILE_WARMUP
#define SPWKIT_STM32_PROFILE_WARMUP 16u
#endif
#ifndef SPWKIT_STM32_PROFILE_ITERATIONS
#define SPWKIT_STM32_PROFILE_ITERATIONS 64u
#endif

#if SPWKIT_STM32_PROFILE_ITERATIONS > 256
#error "STM32 native comparison stores at most 256 paired samples per row"
#endif

typedef struct stm32_native_compare_row {
    uint32_t payload_bytes;
    volatile uint32_t native_samples[SPWKIT_STM32_PROFILE_ITERATIONS];
    volatile uint32_t spwkit_samples[SPWKIT_STM32_PROFILE_ITERATIONS];
} stm32_native_compare_row_t;

typedef struct stm32_native_compare_evidence {
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
    stm32_native_compare_row_t rows[STM32_PROFILE_PAYLOAD_COUNT];
} stm32_native_compare_evidence_t;

volatile stm32_native_compare_evidence_t g_stm32h755_spwkit_profile = {
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
    .row_count = STM32_PROFILE_PAYLOAD_COUNT
};

static uint8_t g_native_compare_tx_data[STM32_DMA_PACKET_CAPACITY];
static uint8_t g_native_compare_rx_data[STM32_DMA_PACKET_CAPACITY];
static volatile uint32_t g_native_compare_sink;

static const uint32_t STM32_PROFILE_PAYLOADS[STM32_PROFILE_PAYLOAD_COUNT] = {
    0u, 1u, 8u, 64u, 256u
};

static void native_compare_fill(uint8_t* data, size_t length, uint8_t seed) {
    size_t index;
    for (index = 0u; index < length; ++index) {
        data[index] = (uint8_t)(seed + (uint8_t)index);
    }
}

static int native_compare_capture_delta(uint32_t* out_cycles) {
    const volatile spw_profile_sample_t* sample = spw_profile_last_sample();
    if (out_cycles == NULL || sample == NULL || sample->sequence != 1u ||
        sample->delta > UINT32_MAX) {
        return 0;
    }
    *out_cycles = (uint32_t)sample->delta;
    return 1;
}

static int native_compare_make_port(spw_port_t** out_port) {
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

static int native_compare_direct_rx_cleanup(void) {
    spw_driver_buffer_t rx = {0};
    spw_driver_buffer_t tx = {0};

    if (driver_acquire_rx(&g_driver, SPW_TIMEOUT_IMMEDIATE, &rx) != SPW_OK ||
        driver_release_rx(&g_driver, &rx) != SPW_OK ||
        driver_reclaim_tx(&g_driver, SPW_TIMEOUT_IMMEDIATE, &tx) != SPW_OK ||
        driver_release_tx(&g_driver, &tx) != SPW_OK) {
        return 0;
    }
    return 1;
}

static int native_compare_spwkit_rx_cleanup(spw_port_t* port) {
    spw_buffer_t* rx = NULL;
    spw_buffer_t* tx = NULL;

    if (spw_port_acquire_rx_buffer(port, SPW_TIMEOUT_IMMEDIATE, &rx) != SPW_OK ||
        rx == NULL ||
        spw_port_release_rx_buffer(port, &rx) != SPW_OK || rx != NULL ||
        spw_port_reclaim_tx_buffer(port, SPW_TIMEOUT_IMMEDIATE, &tx) != SPW_OK ||
        tx == NULL ||
        spw_port_release_tx_buffer(port, &tx) != SPW_OK || tx != NULL) {
        return 0;
    }
    return 1;
}

static int native_compare_direct_copied_tx(size_t length,
                                           uint8_t seed,
                                           uint32_t* out_cycles) {
    spw_packet_t tx = {
        g_native_compare_tx_data, length, length, SPW_TERMINATOR_EOP
    };
    spw_packet_t rx = {
        g_native_compare_rx_data, 0u, sizeof(g_native_compare_rx_data), SPW_TERMINATOR_EOP
    };

    native_compare_fill(g_native_compare_tx_data, length, seed);
    spw_profile_reset();
    SPW_PROFILE_CAPTURE_START();
    if (driver_send(&g_driver, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK ||
        !native_compare_capture_delta(out_cycles) ||
        driver_receive(&g_driver, &rx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK ||
        rx.length != length) {
        return 0;
    }
    return 1;
}

static int native_compare_spwkit_copied_tx(spw_port_t* port,
                                           size_t length,
                                           uint8_t seed,
                                           uint32_t* out_cycles) {
    spw_packet_t tx = {
        g_native_compare_tx_data, length, length, SPW_TERMINATOR_EOP
    };
    spw_packet_t rx = {
        g_native_compare_rx_data, 0u, sizeof(g_native_compare_rx_data), SPW_TERMINATOR_EOP
    };

    native_compare_fill(g_native_compare_tx_data, length, seed);
    spw_profile_reset();
    if (spw_port_send(port, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK ||
        !native_compare_capture_delta(out_cycles) ||
        spw_port_receive(port, &rx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK ||
        rx.length != length) {
        return 0;
    }
    return 1;
}

static int native_compare_direct_zc_submit(size_t length,
                                           uint8_t seed,
                                           uint32_t* out_cycles) {
    spw_driver_buffer_t tx = {0};

    if (driver_acquire_tx(&g_driver, length, SPW_TIMEOUT_IMMEDIATE, &tx) != SPW_OK ||
        tx.data != g_dma_tx || tx.capacity < length) {
        return 0;
    }
    native_compare_fill(tx.data, length, seed);
    tx.length = length;
    tx.terminator = SPW_TERMINATOR_EOP;

    spw_profile_reset();
    SPW_PROFILE_CAPTURE_START();
    if (driver_sync(&g_driver, &tx, SPW_DRIVER_SYNC_TO_DEVICE) != SPW_OK ||
        driver_submit_tx(&g_driver, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK ||
        !native_compare_capture_delta(out_cycles)) {
        return 0;
    }
    return native_compare_direct_rx_cleanup();
}

static int native_compare_spwkit_zc_submit(spw_port_t* port,
                                           size_t length,
                                           uint8_t seed,
                                           uint32_t* out_cycles) {
    spw_buffer_t* tx = NULL;
    spw_buffer_view_t view = {0};

    if (spw_port_acquire_tx_buffer(port, length, SPW_TIMEOUT_IMMEDIATE, &tx) != SPW_OK ||
        tx == NULL || spw_buffer_get_view(tx, &view) != SPW_OK ||
        view.capacity < length) {
        return 0;
    }
    native_compare_fill(view.data, length, seed);
    if (spw_buffer_set_packet(tx, length, SPW_TERMINATOR_EOP) != SPW_OK) {
        return 0;
    }

    spw_profile_reset();
    if (spw_port_submit_tx_buffer(port, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK ||
        tx != NULL || !native_compare_capture_delta(out_cycles)) {
        return 0;
    }
    return native_compare_spwkit_rx_cleanup(port);
}

static int native_compare_direct_copied_rx(size_t length,
                                           uint8_t seed,
                                           uint32_t* out_cycles) {
    spw_packet_t tx = {
        g_native_compare_tx_data, length, length, SPW_TERMINATOR_EOP
    };
    spw_packet_t rx = {
        g_native_compare_rx_data, 0u, sizeof(g_native_compare_rx_data), SPW_TERMINATOR_EOP
    };

    native_compare_fill(g_native_compare_tx_data, length, seed);
    spw_profile_reset();
    if (driver_send(&g_driver, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK ||
        driver_receive(&g_driver, &rx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK) {
        return 0;
    }
    SPW_PROFILE_CAPTURE_END();
    if (rx.length != length || !native_compare_capture_delta(out_cycles)) {
        return 0;
    }
    return 1;
}

static int native_compare_spwkit_copied_rx(spw_port_t* port,
                                           size_t length,
                                           uint8_t seed,
                                           uint32_t* out_cycles) {
    spw_packet_t tx = {
        g_native_compare_tx_data, length, length, SPW_TERMINATOR_EOP
    };
    spw_packet_t rx = {
        g_native_compare_rx_data, 0u, sizeof(g_native_compare_rx_data), SPW_TERMINATOR_EOP
    };

    native_compare_fill(g_native_compare_tx_data, length, seed);
    spw_profile_reset();
    if (spw_port_send(port, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK ||
        spw_port_receive(port, &rx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK ||
        rx.length != length || !native_compare_capture_delta(out_cycles)) {
        return 0;
    }
    return 1;
}

static int native_compare_direct_zc_rx(size_t length,
                                       uint8_t seed,
                                       uint32_t* out_cycles) {
    spw_driver_buffer_t tx = {0};
    spw_driver_buffer_t rx = {0};
    spw_driver_buffer_t reclaimed = {0};

    if (driver_acquire_tx(&g_driver, length, SPW_TIMEOUT_IMMEDIATE, &tx) != SPW_OK ||
        tx.data != g_dma_tx || tx.capacity < length) {
        return 0;
    }
    native_compare_fill(tx.data, length, seed);
    tx.length = length;
    tx.terminator = SPW_TERMINATOR_EOP;
    if (driver_sync(&g_driver, &tx, SPW_DRIVER_SYNC_TO_DEVICE) != SPW_OK) {
        return 0;
    }

    if (length == 0u) {
        spw_profile_reset();
    }
    if (driver_submit_tx(&g_driver, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK) {
        return 0;
    }
    if (length != 0u) {
        spw_profile_reset();
    }
    if (driver_acquire_rx(&g_driver, SPW_TIMEOUT_IMMEDIATE, &rx) != SPW_OK) {
        return 0;
    }
    SPW_PROFILE_CAPTURE_END();
    if (!native_compare_capture_delta(out_cycles) ||
        rx.length != length ||
        driver_release_rx(&g_driver, &rx) != SPW_OK ||
        driver_reclaim_tx(&g_driver, SPW_TIMEOUT_IMMEDIATE, &reclaimed) != SPW_OK ||
        driver_release_tx(&g_driver, &reclaimed) != SPW_OK) {
        return 0;
    }
    return 1;
}

static int native_compare_spwkit_zc_rx(spw_port_t* port,
                                       size_t length,
                                       uint8_t seed,
                                       uint32_t* out_cycles) {
    spw_buffer_t* tx = NULL;
    spw_buffer_t* rx = NULL;
    spw_buffer_view_t tx_view = {0};
    spw_buffer_view_t rx_view = {0};

    if (spw_port_acquire_tx_buffer(port, length, SPW_TIMEOUT_IMMEDIATE, &tx) != SPW_OK ||
        tx == NULL || spw_buffer_get_view(tx, &tx_view) != SPW_OK ||
        tx_view.capacity < length) {
        return 0;
    }
    native_compare_fill(tx_view.data, length, seed);
    if (spw_buffer_set_packet(tx, length, SPW_TERMINATOR_EOP) != SPW_OK) {
        return 0;
    }

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
        rx == NULL || spw_buffer_get_view(rx, &rx_view) != SPW_OK ||
        rx_view.length != length || !native_compare_capture_delta(out_cycles) ||
        spw_port_release_rx_buffer(port, &rx) != SPW_OK || rx != NULL) {
        return 0;
    }
    {
        spw_buffer_t* reclaimed = NULL;
        if (spw_port_reclaim_tx_buffer(port, SPW_TIMEOUT_IMMEDIATE, &reclaimed) != SPW_OK ||
            reclaimed == NULL ||
            spw_port_release_tx_buffer(port, &reclaimed) != SPW_OK || reclaimed != NULL) {
            return 0;
        }
    }
    return 1;
}

static int native_compare_one(spw_port_t* port,
                              size_t length,
                              uint8_t seed,
                              int native_path,
                              uint32_t* out_cycles) {
#if SPWKIT_STM32_PROFILE_CASE == 9
    return native_path
        ? native_compare_direct_copied_tx(length, seed, out_cycles)
        : native_compare_spwkit_copied_tx(port, length, seed, out_cycles);
#elif SPWKIT_STM32_PROFILE_CASE == 10
    return native_path
        ? native_compare_direct_zc_submit(length, seed, out_cycles)
        : native_compare_spwkit_zc_submit(port, length, seed, out_cycles);
#elif SPWKIT_STM32_PROFILE_CASE == 11
    return native_path
        ? native_compare_direct_copied_rx(length, seed, out_cycles)
        : native_compare_spwkit_copied_rx(port, length, seed, out_cycles);
#elif SPWKIT_STM32_PROFILE_CASE == 12
    return native_path
        ? native_compare_direct_zc_rx(length, seed, out_cycles)
        : native_compare_spwkit_zc_rx(port, length, seed, out_cycles);
#else
    (void)port;
    (void)length;
    (void)seed;
    (void)native_path;
    (void)out_cycles;
    return 0;
#endif
}

static int native_compare_pair(spw_port_t* port,
                               size_t length,
                               uint32_t index,
                               uint32_t* out_native,
                               uint32_t* out_spwkit) {
    const uint8_t native_seed = (uint8_t)(0x31u + (index & 0x3fu));
    const uint8_t spwkit_seed = (uint8_t)(0x71u + (index & 0x3fu));

    if ((index & 1u) == 0u) {
        if (!native_compare_one(port, length, native_seed, 1, out_native) ||
            !native_compare_one(port, length, spwkit_seed, 0, out_spwkit)) {
            return 0;
        }
    } else {
        if (!native_compare_one(port, length, spwkit_seed, 0, out_spwkit) ||
            !native_compare_one(port, length, native_seed, 1, out_native)) {
            return 0;
        }
    }
    return 1;
}

static void native_compare_measure_floor(void) {
    uint32_t index;
    for (index = 0u; index < (uint32_t)SPWKIT_STM32_PROFILE_WARMUP; ++index) {
        const uint64_t start = spw_profile_counter_read();
        const uint64_t end = spw_profile_counter_read();
        g_native_compare_sink ^= (uint32_t)spw_profile_counter_delta(start, end);
    }
    for (index = 0u; index < (uint32_t)SPWKIT_STM32_PROFILE_ITERATIONS; ++index) {
        const uint64_t start = spw_profile_counter_read();
        const uint64_t end = spw_profile_counter_read();
        g_stm32h755_spwkit_profile.floor_samples[index] =
            (uint32_t)spw_profile_counter_delta(start, end);
    }
}

static int run_profile(void) {
    spw_port_t* port = NULL;
    uint32_t row;
    uint32_t index;
    uint32_t native_cycles = 0u;
    uint32_t spwkit_cycles = 0u;

    g_stm32h755_spwkit_profile.phase = 1u;
    spw_profile_prepare();
    native_compare_measure_floor();

    if (!native_compare_make_port(&port)) {
        return 0xc01;
    }

    for (row = 0u; row < STM32_PROFILE_PAYLOAD_COUNT; ++row) {
        const size_t length = (size_t)STM32_PROFILE_PAYLOADS[row];
        g_stm32h755_spwkit_profile.phase = 0x200u + row;
        g_stm32h755_spwkit_profile.rows[row].payload_bytes = (uint32_t)length;

        for (index = 0u; index < (uint32_t)SPWKIT_STM32_PROFILE_WARMUP; ++index) {
            if (!native_compare_pair(port, length, index, &native_cycles, &spwkit_cycles)) {
                return (int)(0xd000u | (row << 8u) | (index & 0xffu));
            }
        }
        for (index = 0u; index < (uint32_t)SPWKIT_STM32_PROFILE_ITERATIONS; ++index) {
            if (!native_compare_pair(port, length, index, &native_cycles, &spwkit_cycles)) {
                return (int)(0xe000u | (row << 8u) | (index & 0xffu));
            }
            g_stm32h755_spwkit_profile.rows[row].native_samples[index] = native_cycles;
            g_stm32h755_spwkit_profile.rows[row].spwkit_samples[index] = spwkit_cycles;
        }
    }

    if (spw_port_close(port) != SPW_OK) {
        return 0xf01;
    }
    return 0;
}
