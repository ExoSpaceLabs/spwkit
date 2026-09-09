#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


# ---------------------------------------------------------------------------
# Integration CMake: keep the correctness target unchanged by default and add
# a distinct Release profiling firmware mode that can include the repo-private
# profiling header while linking against the installed, identically configured
# SpWKit library.
# ---------------------------------------------------------------------------
cmake_path = ROOT / "integrations/stm32h755_dma/CMakeLists.txt"
cmake_path.write_text(r'''cmake_minimum_required(VERSION 3.20)
project(spwkit_stm32h755_dma_integration LANGUAGES C ASM)

find_package(SpWKit 0.6 CONFIG REQUIRED)

if(NOT TARGET spwkit::spwkit)
    message(FATAL_ERROR "Installed SpWKit package does not export spwkit::spwkit")
endif()

set(STM32_CUBE_H7_DIR "" CACHE PATH
    "Path to a pinned STM32CubeH7 checkout containing Drivers/CMSIS")
option(SPWKIT_STM32_PROFILE_FIRMWARE
    "Build the STM32H755 Release/DWT profiling firmware instead of correctness evidence" OFF)
set(SPWKIT_SOURCE_DIR "" CACHE PATH
    "SpWKit source root, required only by the repo-internal profiling firmware")
set(SPWKIT_STM32_PROFILE_START "" CACHE STRING "Profiling start probe name")
set(SPWKIT_STM32_PROFILE_END "" CACHE STRING "Profiling end probe name")
set(SPWKIT_STM32_PROFILE_CASE "0" CACHE STRING "STM32 profile case id")
set(SPWKIT_STM32_PROFILE_WARMUP "16" CACHE STRING "Warmup iterations per payload")
set(SPWKIT_STM32_PROFILE_ITERATIONS "64" CACHE STRING "Measured iterations per payload")

if(NOT STM32_CUBE_H7_DIR)
    message(FATAL_ERROR
        "STM32_CUBE_H7_DIR is required. Use the pinned STM32CubeH7 release documented in README.md")
endif()

set(STM32_CMSIS_CORE_INCLUDE
    "${STM32_CUBE_H7_DIR}/Drivers/CMSIS/Include")
set(STM32_CMSIS_DEVICE_INCLUDE
    "${STM32_CUBE_H7_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx/Include")

if(NOT EXISTS "${STM32_CMSIS_CORE_INCLUDE}/core_cm7.h")
    message(FATAL_ERROR
        "CMSIS Cortex-M7 core headers were not found under ${STM32_CMSIS_CORE_INCLUDE}")
endif()
if(NOT EXISTS "${STM32_CMSIS_DEVICE_INCLUDE}/stm32h755xx.h")
    message(FATAL_ERROR
        "STM32H755 CMSIS device headers were not found under ${STM32_CMSIS_DEVICE_INCLUDE}; initialize the STM32H7xx CMSIS device submodule")
endif()

function(spwkit_configure_stm32_target target)
    target_link_libraries(${target} PRIVATE spwkit::spwkit)
    target_include_directories(${target} PRIVATE
        "${STM32_CMSIS_CORE_INCLUDE}"
        "${STM32_CMSIS_DEVICE_INCLUDE}")
    target_compile_definitions(${target} PRIVATE
        CORE_CM7
        STM32H755xx)
    target_compile_options(${target} PRIVATE
        -Wall
        -Wextra
        -Werror
        -ffreestanding
        -fno-builtin)
    target_link_options(${target} PRIVATE
        -nostartfiles
        --specs=nosys.specs
        -Wl,--gc-sections
        -Wl,--unresolved-symbols=report-all
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${target}.map
        -T${CMAKE_CURRENT_SOURCE_DIR}/linker.ld)
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS OFF
        SUFFIX ".elf")
endfunction()

if(SPWKIT_STM32_PROFILE_FIRMWARE)
    if(NOT SPWKIT_SOURCE_DIR OR
       NOT EXISTS "${SPWKIT_SOURCE_DIR}/src/profiling/profile.h")
        message(FATAL_ERROR
            "SPWKIT_SOURCE_DIR must point at the SpWKit source tree for profiling firmware")
    endif()
    if(SPWKIT_STM32_PROFILE_START STREQUAL "" OR
       SPWKIT_STM32_PROFILE_END STREQUAL "")
        message(FATAL_ERROR
            "Profiling firmware requires SPWKIT_STM32_PROFILE_START and SPWKIT_STM32_PROFILE_END")
    endif()
    if(NOT SPWKIT_STM32_PROFILE_CASE MATCHES "^[1-8]$")
        message(FATAL_ERROR "SPWKIT_STM32_PROFILE_CASE must be in the range 1..8")
    endif()
    if(NOT SPWKIT_STM32_PROFILE_WARMUP MATCHES "^[0-9]+$" OR
       NOT SPWKIT_STM32_PROFILE_ITERATIONS MATCHES "^[1-9][0-9]*$")
        message(FATAL_ERROR "STM32 profiling warmup/iterations must be non-negative/positive integers")
    endif()
    if(SPWKIT_STM32_PROFILE_ITERATIONS GREATER 256)
        message(FATAL_ERROR "STM32 profiling iterations must not exceed 256")
    endif()

    add_executable(spwkit_stm32h755_profile firmware.c)
    spwkit_configure_stm32_target(spwkit_stm32h755_profile)
    target_include_directories(spwkit_stm32h755_profile PRIVATE
        "${SPWKIT_SOURCE_DIR}/src")
    target_compile_definitions(spwkit_stm32h755_profile PRIVATE
        SPWKIT_STM32_PROFILE=1
        SPWKIT_ENABLE_PROFILING=1
        SPWKIT_PROFILE_START=${SPWKIT_STM32_PROFILE_START}
        SPWKIT_PROFILE_END=${SPWKIT_STM32_PROFILE_END}
        SPWKIT_STM32_PROFILE_CASE=${SPWKIT_STM32_PROFILE_CASE}
        SPWKIT_STM32_PROFILE_WARMUP=${SPWKIT_STM32_PROFILE_WARMUP}
        SPWKIT_STM32_PROFILE_ITERATIONS=${SPWKIT_STM32_PROFILE_ITERATIONS})
    # Debug information is retained only so GDB can extract the fixed result
    # structure. -g does not change the Release optimization level.
    target_compile_options(spwkit_stm32h755_profile PRIVATE -g3)
else()
    add_executable(spwkit_stm32h755_dma firmware.c)
    spwkit_configure_stm32_target(spwkit_stm32h755_dma)
endif()
''')


# ---------------------------------------------------------------------------
# Profile implementation lives as an include in the same translation unit so
# it can exercise the real static STM32 provider without making those internals
# public or duplicating the driver implementation.
# ---------------------------------------------------------------------------
profile_inc = ROOT / "integrations/stm32h755_dma/profile_impl.inc"
profile_inc.write_text(r'''/* SPDX-License-Identifier: Apache-2.0 */
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

static int profile_copied_once(spw_port_t* port,
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

static int profile_prepare_zc_tx(spw_port_t* port,
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

static int profile_release_rx_ready(spw_port_t* port) {
    spw_buffer_t* rx = NULL;
    if (spw_port_acquire_rx_buffer(port, SPW_TIMEOUT_IMMEDIATE, &rx) != SPW_OK ||
        rx == NULL) {
        return 0;
    }
    return spw_port_release_rx_buffer(port, &rx) == SPW_OK && rx == NULL;
}

static int profile_reclaim_release_tx(spw_port_t* port) {
    spw_buffer_t* reclaimed = NULL;
    if (spw_port_reclaim_tx_buffer(port, SPW_TIMEOUT_IMMEDIATE, &reclaimed) != SPW_OK ||
        reclaimed == NULL) {
        return 0;
    }
    return spw_port_release_tx_buffer(port, &reclaimed) == SPW_OK && reclaimed == NULL;
}

static int profile_zc_once(spw_port_t* port, size_t length, uint32_t* out_cycles) {
    spw_buffer_t* tx = NULL;
    spw_buffer_t* reclaimed = NULL;
    spw_buffer_t* rx = NULL;

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
    if (!profile_prepare_zc_tx(port, length, &tx) ||
        spw_port_submit_tx_buffer(port, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK || tx != NULL) {
        return 0;
    }
    spw_profile_reset();
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
    if (!profile_prepare_zc_tx(port, length, &tx) ||
        spw_port_submit_tx_buffer(port, &tx, SPW_TIMEOUT_IMMEDIATE) != SPW_OK || tx != NULL) {
        return 0;
    }
    spw_profile_reset();
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
''')


# ---------------------------------------------------------------------------
# Provider-owned profile boundaries and the profile/reset dispatch.
# ---------------------------------------------------------------------------
firmware_path = ROOT / "integrations/stm32h755_dma/firmware.c"
firmware = firmware_path.read_text()

firmware = replace_once(
    firmware,
    '#include "stm32h755xx.h"\n\n#include <stdalign.h>\n',
    '#include "stm32h755xx.h"\n\n#ifdef SPWKIT_STM32_PROFILE\n#include "profiling/profile.h"\n#define STM32_PROFILE_DMA_SUBMITTED() do { \\\n    SPW_PROFILE_TX_PROVIDER_BOUNDARY(); \\\n    SPW_PROFILE_TX_ZC_SUBMIT_PROVIDER_BOUNDARY(); \\\n} while (0)\n#define STM32_PROFILE_DMA_COMPLETE() do { \\\n    SPW_PROFILE_RX_PROVIDER_BOUNDARY(); \\\n    SPW_PROFILE_RX_ZC_ACQUIRE_PROVIDER_BOUNDARY(); \\\n    SPW_PROFILE_TX_ZC_RECLAIM_PROVIDER_BOUNDARY(); \\\n} while (0)\n#else\n#define STM32_PROFILE_DMA_SUBMITTED() ((void)0)\n#define STM32_PROFILE_DMA_COMPLETE() ((void)0)\n#endif\n\n#include <stdalign.h>\n',
    "profile header/wrappers",
)

firmware = replace_once(
    firmware,
    '    if (length == 0u) {\n        return 1;\n    }\n',
    '    if (length == 0u) {\n        STM32_PROFILE_DMA_SUBMITTED();\n        return 1;\n    }\n',
    "zero-length submission boundary",
)

firmware = replace_once(
    firmware,
    '    DMA2_Stream0->CR |= DMA_SxCR_EN;\n    return 1;\n',
    '    DMA2_Stream0->CR |= DMA_SxCR_EN;\n    STM32_PROFILE_DMA_SUBMITTED();\n    return 1;\n',
    "DMA submission boundary",
)

firmware = replace_once(
    firmware,
    '    state->tx_owner = STM32_DMA_TX_COMPLETE;\n    state->rx_owner = STM32_DMA_RX_READY;\n    ++state->dma_transfers;\n}\n',
    '    state->tx_owner = STM32_DMA_TX_COMPLETE;\n    state->rx_owner = STM32_DMA_RX_READY;\n    ++state->dma_transfers;\n    STM32_PROFILE_DMA_COMPLETE();\n}\n',
    "DMA completion boundary",
)

# Both copied and zero-copy zero-length paths complete without driver_poll_completion.
firmware = replace_once(
    firmware,
    '        state->tx_owner = STM32_DMA_TX_COMPLETE;\n        state->rx_owner = STM32_DMA_RX_READY;\n        ++state->dma_transfers;\n    } else {\n        driver_poll_completion(state);\n',
    '        state->tx_owner = STM32_DMA_TX_COMPLETE;\n        state->rx_owner = STM32_DMA_RX_READY;\n        ++state->dma_transfers;\n        STM32_PROFILE_DMA_COMPLETE();\n    } else {\n        driver_poll_completion(state);\n',
    "copied zero-length completion",
)

firmware = replace_once(
    firmware,
    '        state->tx_owner = STM32_DMA_TX_COMPLETE;\n        state->rx_owner = STM32_DMA_RX_READY;\n        ++state->dma_transfers;\n    }\n    ++state->statistics.tx_packets;\n',
    '        state->tx_owner = STM32_DMA_TX_COMPLETE;\n        state->rx_owner = STM32_DMA_RX_READY;\n        ++state->dma_transfers;\n        STM32_PROFILE_DMA_COMPLETE();\n    }\n    ++state->statistics.tx_packets;\n',
    "zero-copy zero-length completion",
)

firmware = replace_once(
    firmware,
    'static const spw_driver_ops_t STM32_DRIVER_OPS = {\n    sizeof(spw_driver_ops_t), SPW_DRIVER_OPS_VERSION,\n    driver_start, driver_stop, driver_reset,\n    driver_get_link_state, driver_get_capabilities,\n    driver_send, driver_receive,\n    NULL, NULL,\n    driver_get_statistics, driver_clear_statistics,\n    NULL,\n    driver_acquire_tx, driver_submit_tx,\n    driver_reclaim_tx, driver_release_tx,\n    driver_acquire_rx, driver_release_rx,\n    driver_sync\n};\n\nstatic int run_contract(void) {\n',
    'static const spw_driver_ops_t STM32_DRIVER_OPS = {\n    sizeof(spw_driver_ops_t), SPW_DRIVER_OPS_VERSION,\n    driver_start, driver_stop, driver_reset,\n    driver_get_link_state, driver_get_capabilities,\n    driver_send, driver_receive,\n    NULL, NULL,\n    driver_get_statistics, driver_clear_statistics,\n    NULL,\n    driver_acquire_tx, driver_submit_tx,\n    driver_reclaim_tx, driver_release_tx,\n    driver_acquire_rx, driver_release_rx,\n    driver_sync\n};\n\n#ifdef SPWKIT_STM32_PROFILE\n#include "profile_impl.inc"\n#endif\n\nstatic int run_contract(void) {\n',
    "profile implementation include",
)

firmware = replace_once(
    firmware,
    '    g_driver.link_state = SPW_LINK_ERROR_RESET;\n    g_driver.tx_owner = STM32_DMA_FREE;\n    g_driver.rx_owner = STM32_DMA_FREE;\n    g_stm32h755_spwkit_evidence.magic = STM32_EVIDENCE_MAGIC;\n    g_stm32h755_spwkit_evidence.result = UINT32_C(0xfffffffe);\n\n    result = run_contract();\n    g_stm32h755_spwkit_evidence.result = (uint32_t)result;\n    g_stm32h755_spwkit_evidence.phase = result == 0 ? 0x700du : 0xdead0000u | (uint32_t)result;\n    __DSB();\n',
    '    g_driver.link_state = SPW_LINK_ERROR_RESET;\n    g_driver.tx_owner = STM32_DMA_FREE;\n    g_driver.rx_owner = STM32_DMA_FREE;\n\n#ifdef SPWKIT_STM32_PROFILE\n    g_stm32h755_spwkit_profile.magic = STM32_PROFILE_MAGIC;\n    g_stm32h755_spwkit_profile.result = UINT32_C(0xfffffffe);\n    result = run_profile();\n    g_stm32h755_spwkit_profile.result = (uint32_t)result;\n    g_stm32h755_spwkit_profile.phase =\n        result == 0 ? 0x700du : 0xdead0000u | (uint32_t)result;\n#else\n    g_stm32h755_spwkit_evidence.magic = STM32_EVIDENCE_MAGIC;\n    g_stm32h755_spwkit_evidence.result = UINT32_C(0xfffffffe);\n    result = run_contract();\n    g_stm32h755_spwkit_evidence.result = (uint32_t)result;\n    g_stm32h755_spwkit_evidence.phase =\n        result == 0 ? 0x700du : 0xdead0000u | (uint32_t)result;\n#endif\n    __DSB();\n',
    "profile Reset_Handler dispatch",
)
firmware_path.write_text(firmware)


# ---------------------------------------------------------------------------
# GDB extraction script. Raw samples are printed as integer core-cycle values;
# the host parser computes summary statistics without floating-point firmware.
# ---------------------------------------------------------------------------
gdb_path = ROOT / "scripts/gdb/stm32h755_profile.gdb"
gdb_path.write_text(r'''set confirm off
set pagination off
set print pretty off
set mem inaccessible-by-default off

target extended-remote :3333
monitor arm semihosting disable
monitor reset halt

printf "Flashing STM32H755 CM7 profiling image...\n"
load
compare-sections

printf "Running SpWKit STM32H755 DWT profiling firmware...\n"
monitor reset run
shell sleep 2
monitor halt

set $magic=(unsigned int)g_stm32h755_spwkit_profile.magic
set $version=(unsigned int)g_stm32h755_spwkit_profile.version
set $phase=(unsigned int)g_stm32h755_spwkit_profile.phase
set $result=(unsigned int)g_stm32h755_spwkit_profile.result
set $case=(unsigned int)g_stm32h755_spwkit_profile.case_id
set $start=(unsigned int)g_stm32h755_spwkit_profile.start_probe_id
set $end=(unsigned int)g_stm32h755_spwkit_profile.end_probe_id
set $hz=(unsigned int)g_stm32h755_spwkit_profile.core_hz
set $warmup=(unsigned int)g_stm32h755_spwkit_profile.warmup_iterations
set $iterations=(unsigned int)g_stm32h755_spwkit_profile.measured_iterations
set $rows=(unsigned int)g_stm32h755_spwkit_profile.row_count
set $cache=(unsigned int)g_stm32h755_spwkit_profile.cache_valid

printf "PROFILE_META magic=0x%08x version=%u phase=0x%08x result=0x%08x case_id=%u start_id=%u end_id=%u core_hz=%u warmup=%u iterations=%u rows=%u cache_valid=%u\n", $magic, $version, $phase, $result, $case, $start, $end, $hz, $warmup, $iterations, $rows, $cache

set $i=0
while $i < $iterations
  printf "PROFILE_FLOOR index=%u cycles=%u\n", $i, (unsigned int)g_stm32h755_spwkit_profile.floor_samples[$i]
  set $i=$i+1
end

set $r=0
while $r < $rows
  set $payload=(unsigned int)g_stm32h755_spwkit_profile.rows[$r].payload_bytes
  set $i=0
  while $i < $iterations
    printf "PROFILE_SAMPLE row=%u payload=%u index=%u cycles=%u\n", $r, $payload, $i, (unsigned int)g_stm32h755_spwkit_profile.rows[$r].samples[$i]
    set $i=$i+1
  end
  set $r=$r+1
end

if $cache == 1
  set $r=0
  while $r < $rows
    set $payload=(unsigned int)g_stm32h755_spwkit_profile.cache_clean_rows[$r].payload_bytes
    set $i=0
    while $i < $iterations
      printf "PROFILE_CACHE_CLEAN row=%u payload=%u index=%u cycles=%u\n", $r, $payload, $i, (unsigned int)g_stm32h755_spwkit_profile.cache_clean_rows[$r].samples[$i]
      printf "PROFILE_CACHE_INVALIDATE row=%u payload=%u index=%u cycles=%u\n", $r, $payload, $i, (unsigned int)g_stm32h755_spwkit_profile.cache_invalidate_rows[$r].samples[$i]
      set $i=$i+1
    end
    set $r=$r+1
  end
end

if $magic == 0x53575050 && $version == 1 && $phase == 0x0000700d && $result == 0 && $iterations > 0 && $rows == 5
  printf "RESULT: PASS\n"
else
  printf "RESULT: FAIL\n"
end

monitor resume
detach
quit
''')


# ---------------------------------------------------------------------------
# Parse GDB records into exact JSON with raw samples and full-precision means.
# Human summaries round medians/means to integral core cycles.
# ---------------------------------------------------------------------------
extract_path = ROOT / "scripts/extract_stm32h755_profile.py"
extract_path.write_text(r'''#!/usr/bin/env python3
import argparse
import json
import math
import re
from pathlib import Path

META_RE = re.compile(r"PROFILE_META (.+)$")
SAMPLE_RE = re.compile(r"PROFILE_SAMPLE row=(\d+) payload=(\d+) index=(\d+) cycles=(\d+)")
FLOOR_RE = re.compile(r"PROFILE_FLOOR index=(\d+) cycles=(\d+)")
CLEAN_RE = re.compile(r"PROFILE_CACHE_CLEAN row=(\d+) payload=(\d+) index=(\d+) cycles=(\d+)")
INVALIDATE_RE = re.compile(r"PROFILE_CACHE_INVALIDATE row=(\d+) payload=(\d+) index=(\d+) cycles=(\d+)")


def parse_int(value):
    return int(value, 0)


def nearest_rank(values, percentile):
    ordered = sorted(values)
    rank = max(1, math.ceil(percentile * len(ordered)))
    return ordered[rank - 1]


def statistics(values):
    if not values:
        raise ValueError("empty sample set")
    ordered = sorted(values)
    n = len(ordered)
    if n % 2:
        median = float(ordered[n // 2])
    else:
        median = (ordered[n // 2 - 1] + ordered[n // 2]) / 2.0
    mean = sum(ordered) / n
    variance = sum((value - mean) ** 2 for value in ordered) / n
    return {
        "min": ordered[0],
        "median": median,
        "mean": mean,
        "p95": nearest_rank(ordered, 0.95),
        "p99": nearest_rank(ordered, 0.99),
        "max": ordered[-1],
        "stddev": math.sqrt(variance),
    }


def add_sample(rows, row, payload, index, cycles):
    entry = rows.setdefault(row, {"payload_bytes": payload, "samples": {}})
    if entry["payload_bytes"] != payload:
        raise ValueError(f"payload changed within row {row}")
    entry["samples"][index] = cycles


def finalize_rows(rows, iterations):
    result = []
    for row_index in sorted(rows):
        entry = rows[row_index]
        samples = [entry["samples"].get(i) for i in range(iterations)]
        if any(value is None for value in samples):
            raise ValueError(f"row {row_index} does not contain {iterations} samples")
        result.append({
            "payload_bytes": entry["payload_bytes"],
            "samples": samples,
            "statistics": statistics(samples),
        })
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument("--case", required=True)
    parser.add_argument("--start", required=True)
    parser.add_argument("--end", required=True)
    parser.add_argument("--git-sha", required=True)
    parser.add_argument("--cube-sha", required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    meta = None
    floor = {}
    rows = {}
    clean = {}
    invalidate = {}
    pass_seen = False

    for line in args.log.read_text(errors="replace").splitlines():
        if line.strip() == "RESULT: PASS":
            pass_seen = True
        match = META_RE.search(line)
        if match:
            meta = {}
            for token in match.group(1).split():
                key, value = token.split("=", 1)
                meta[key] = parse_int(value)
            continue
        match = FLOOR_RE.search(line)
        if match:
            floor[int(match.group(1))] = int(match.group(2))
            continue
        match = SAMPLE_RE.search(line)
        if match:
            add_sample(rows, *(int(group) for group in match.groups()))
            continue
        match = CLEAN_RE.search(line)
        if match:
            add_sample(clean, *(int(group) for group in match.groups()))
            continue
        match = INVALIDATE_RE.search(line)
        if match:
            add_sample(invalidate, *(int(group) for group in match.groups()))

    if meta is None or not pass_seen:
        raise SystemExit("profile log does not contain a passing PROFILE_META/RESULT record")
    iterations = meta["iterations"]
    floor_samples = [floor.get(i) for i in range(iterations)]
    if any(value is None for value in floor_samples):
        raise SystemExit("counter-floor sample set is incomplete")

    payload_rows = finalize_rows(rows, iterations)
    if len(payload_rows) != meta["rows"]:
        raise SystemExit("profile payload row count is incomplete")

    document = {
        "schema": "spwkit.profile.stm32h755.v1",
        "measurement_domain": "software",
        "unit": "core_cycles",
        "result_type": "physical-board",
        "board": "NUCLEO-H755ZI-Q",
        "mcu": "STM32H755ZI",
        "core": "Cortex-M7",
        "git_sha": args.git_sha,
        "stm32cubeh7_sha": args.cube_sha,
        "compiler": args.compiler,
        "build_type": "Release",
        "case": args.case,
        "probe_start": args.start,
        "probe_end": args.end,
        "probe_start_id": meta["start_id"],
        "probe_end_id": meta["end_id"],
        "payload_capacity_bytes": 256,
        "counter": {
            "kind": "cortex-m-dwt-cyccnt",
            "width_bits": 32,
            "frequency_hz": meta["core_hz"],
        },
        "warmup_iterations": meta["warmup"],
        "iterations": iterations,
        "counter_floor": {
            "method": "back_to_back_dwt_reads",
            "samples": floor_samples,
            "statistics": statistics(floor_samples),
            "subtracted": False,
        },
        "rows": payload_rows,
        "scope_note": "STM32 DMA2 memory-to-memory provider timing; not SpaceWire PHY/link timing",
    }

    if meta.get("cache_valid") == 1:
        document["cache_maintenance"] = {
            "clean": finalize_rows(clean, iterations),
            "invalidate": finalize_rows(invalidate, iterations),
            "note": "D-cache primitives isolated around the same CMSIS clean/invalidate helpers used by the provider",
        }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, indent=2) + "\n")


if __name__ == "__main__":
    main()
''')
extract_path.chmod(0o755)

summary_path = ROOT / "scripts/summarize_stm32h755_profile.py"
summary_path.write_text(r'''#!/usr/bin/env python3
import argparse
import json
import math
from pathlib import Path


def rounded(value):
    value = float(value)
    return math.floor(value + 0.5) if value >= 0 else math.ceil(value - 0.5)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("result_dir", type=Path)
    args = parser.parse_args()

    campaign = json.loads((args.result_dir / "campaign.json").read_text())
    files = [args.result_dir / "cases" / f"{name}.json" for name in campaign["cases"]]
    docs = [json.loads(path.read_text()) for path in files]

    lines = [
        "SpWKit STM32H755 Physical Profiling Summary",
        "===========================================",
        f"Result set : {campaign['result_directory_name']}",
        f"Commit     : {campaign['git_sha']}",
        f"Board      : NUCLEO-H755ZI-Q / STM32H755ZI Cortex-M7",
        f"Build      : Release / clean serial configuration builds",
        f"Counter    : DWT CYCCNT / {campaign['core_hz']} Hz",
        f"Payloads   : 0 1 8 64 256 bytes",
        f"Iterations : {campaign['iterations']} measured, {campaign['warmup']} warmup",
        "Display    : cycle values rounded to nearest integer; JSON retains raw samples/full precision",
        "",
    ]

    for doc in docs:
        floor = doc["counter_floor"]["statistics"]
        lines.append(f"{doc['case']}: {doc['probe_start']} -> {doc['probe_end']}")
        lines.append(f"Counter floor median: {rounded(floor['median'])} cycles (diagnostic, not subtracted)")
        lines.append("Payload   Median   Mean   p95   p99")
        for row in doc["rows"]:
            stats = row["statistics"]
            lines.append(
                f"{row['payload_bytes']:>6} B   {rounded(stats['median']):>6}   "
                f"{rounded(stats['mean']):>4}   {stats['p95']:>3}   {stats['p99']:>3}"
            )
        lines.append("")

    cache_doc = next((doc for doc in docs if "cache_maintenance" in doc), None)
    if cache_doc is not None:
        lines.append("Isolated Cortex-M7 D-cache maintenance")
        lines.append("--------------------------------------")
        lines.append("Payload   Clean med   Invalidate med")
        clean = cache_doc["cache_maintenance"]["clean"]
        invalidate = cache_doc["cache_maintenance"]["invalidate"]
        for clean_row, invalidate_row in zip(clean, invalidate):
            lines.append(
                f"{clean_row['payload_bytes']:>6} B   "
                f"{rounded(clean_row['statistics']['median']):>9}   "
                f"{rounded(invalidate_row['statistics']['median']):>14}"
            )
        lines.append("")

    lines.extend([
        "NOTE: These are MCU software/provider/DMA/cache measurements.",
        "They do not include a SpaceWire controller, codec, PHY, cable, or link serialization time.",
        "Standalone DWT floor calibration is diagnostic and is never subtracted from measured intervals.",
        "",
    ])
    summary = "\n".join(lines)
    (args.result_dir / "summary.txt").write_text(summary)
    print(summary, end="")


if __name__ == "__main__":
    main()
''')
summary_path.chmod(0o755)


# ---------------------------------------------------------------------------
# Physical board campaign runner.
# ---------------------------------------------------------------------------
campaign_path = ROOT / "scripts/stm32h755_profile_campaign.sh"
campaign_path.write_text(r'''#!/usr/bin/env bash
set -Eeuo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STM32_CUBE_H7_DIR="${STM32_CUBE_H7_DIR:-}"
OPENOCD_SCRIPTS="${OPENOCD_SCRIPTS:-/usr/share/openocd/scripts}"
BUILD_ROOT="${SPWKIT_STM32_PROFILE_BUILD_ROOT:-$ROOT_DIR/build/stm32-profile-campaign}"
OUTPUT_ROOT="${SPWKIT_STM32_PROFILE_OUTPUT_ROOT:-$ROOT_DIR/build/profile-results}"
DEBUG_TIMEOUT=90
WARMUP=16
ITERATIONS=64
CASES="all"
GDB_BIN=""
OPENOCD_PID=""
PINNED_CUBE_SHA="f5c0b7a2b1f6eb26fde150f72edb2d7deb647066"
PROFILE_CASES=(copied_tx copied_rx zc_tx_acquire zc_tx_submit zc_tx_reclaim zc_tx_release zc_rx_acquire zc_rx_release)

usage() {
  cat <<'USAGE'
Usage:
  scripts/stm32h755_profile_campaign.sh --stm32h7-root /path/to/STM32CubeH7 [options]

Runs the physical NUCLEO-H755ZI-Q profiling configurations strictly serially.
Each selected probe pair gets a clean Release SpWKit build, clean profiling
firmware build, flash/run cycle, DWT calibration, and debugger extraction.

Options:
  --stm32h7-root DIR     pinned STM32CubeH7 checkout root
  --cases "LIST"        all or space/comma-separated profile cases
  --warmup N            warmup iterations per payload (default: 16)
  --iterations N        measured iterations, 1..256 (default: 64)
  --build-root DIR       disposable build root
  --output-root DIR      result parent (default: build/profile-results)
  --openocd-scripts DIR  OpenOCD scripts directory
  --debug-timeout SEC    GDB timeout per case (default: 90)
  --list-cases           print supported profile cases
  -h, --help             show this help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --stm32h7-root) STM32_CUBE_H7_DIR="$2"; shift 2 ;;
    --cases) CASES="$2"; shift 2 ;;
    --warmup) WARMUP="$2"; shift 2 ;;
    --iterations) ITERATIONS="$2"; shift 2 ;;
    --build-root) BUILD_ROOT="$2"; shift 2 ;;
    --output-root) OUTPUT_ROOT="$2"; shift 2 ;;
    --openocd-scripts) OPENOCD_SCRIPTS="$2"; shift 2 ;;
    --debug-timeout) DEBUG_TIMEOUT="$2"; shift 2 ;;
    --list-cases) printf '%s\n' "${PROFILE_CASES[@]}"; exit 0 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

for value in "$WARMUP" "$ITERATIONS" "$DEBUG_TIMEOUT"; do
  [[ "$value" =~ ^[0-9]+$ ]] || { echo "warmup, iterations and timeout must be integers" >&2; exit 2; }
done
(( ITERATIONS >= 1 && ITERATIONS <= 256 )) || { echo "iterations must be 1..256" >&2; exit 2; }
(( DEBUG_TIMEOUT >= 1 )) || { echo "debug-timeout must be positive" >&2; exit 2; }

need() {
  command -v "$1" >/dev/null 2>&1 || { echo "Missing command: $1" >&2; exit 2; }
}
for command in cmake git openocd arm-none-eabi-gcc arm-none-eabi-nm timeout tee grep python3 tar; do
  need "$command"
done
if command -v gdb-multiarch >/dev/null 2>&1; then
  GDB_BIN=gdb-multiarch
elif command -v arm-none-eabi-gdb >/dev/null 2>&1; then
  GDB_BIN=arm-none-eabi-gdb
else
  echo "Install gdb-multiarch or arm-none-eabi-gdb" >&2
  exit 2
fi

[[ -n "$STM32_CUBE_H7_DIR" ]] || { usage >&2; exit 2; }
STM32_CUBE_H7_DIR="$(cd "$STM32_CUBE_H7_DIR" 2>/dev/null && pwd)" || {
  echo "Invalid STM32CubeH7 root: $STM32_CUBE_H7_DIR" >&2
  exit 2
}
for file in \
  "$STM32_CUBE_H7_DIR/Drivers/CMSIS/Include/core_cm7.h" \
  "$STM32_CUBE_H7_DIR/Drivers/CMSIS/Device/ST/STM32H7xx/Include/stm32h755xx.h"; do
  [[ -f "$file" ]] || { echo "Incomplete STM32CubeH7 checkout: missing $file" >&2; exit 2; }
done

CUBE_SHA="unknown"
if git -C "$STM32_CUBE_H7_DIR" rev-parse HEAD >/dev/null 2>&1; then
  CUBE_SHA="$(git -C "$STM32_CUBE_H7_DIR" rev-parse HEAD)"
  if [[ "$CUBE_SHA" != "$PINNED_CUBE_SHA" ]]; then
    echo "WARNING: STM32CubeH7 is $CUBE_SHA; reference evidence is pinned to $PINNED_CUBE_SHA" >&2
  fi
fi

if [[ "$CASES" == "all" ]]; then
  selected_cases=("${PROFILE_CASES[@]}")
else
  CASES="${CASES//,/ }"
  read -r -a selected_cases <<< "$CASES"
fi
(( ${#selected_cases[@]} > 0 )) || { echo "at least one case is required" >&2; exit 2; }
for selected in "${selected_cases[@]}"; do
  found=0
  for known in "${PROFILE_CASES[@]}"; do [[ "$selected" == "$known" ]] && found=1; done
  (( found == 1 )) || { echo "unknown profile case: $selected" >&2; exit 2; }
done

case_config() {
  case "$1" in
    copied_tx)      CASE_ID=1; START=SPW_PROFILE_ID_TX_API_ENTRY; END=SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY ;;
    copied_rx)      CASE_ID=2; START=SPW_PROFILE_ID_RX_PROVIDER_BOUNDARY; END=SPW_PROFILE_ID_RX_API_RETURN ;;
    zc_tx_acquire)  CASE_ID=3; START=SPW_PROFILE_ID_TX_ZC_ACQUIRE_API_ENTRY; END=SPW_PROFILE_ID_TX_ZC_ACQUIRE_API_RETURN ;;
    zc_tx_submit)   CASE_ID=4; START=SPW_PROFILE_ID_TX_ZC_SUBMIT_API_ENTRY; END=SPW_PROFILE_ID_TX_ZC_SUBMIT_PROVIDER_BOUNDARY ;;
    zc_tx_reclaim)  CASE_ID=5; START=SPW_PROFILE_ID_TX_ZC_RECLAIM_PROVIDER_BOUNDARY; END=SPW_PROFILE_ID_TX_ZC_RECLAIM_API_RETURN ;;
    zc_tx_release)  CASE_ID=6; START=SPW_PROFILE_ID_TX_ZC_RELEASE_API_ENTRY; END=SPW_PROFILE_ID_TX_ZC_RELEASE_API_RETURN ;;
    zc_rx_acquire)  CASE_ID=7; START=SPW_PROFILE_ID_RX_ZC_ACQUIRE_PROVIDER_BOUNDARY; END=SPW_PROFILE_ID_RX_ZC_ACQUIRE_API_RETURN ;;
    zc_rx_release)  CASE_ID=8; START=SPW_PROFILE_ID_RX_ZC_RELEASE_API_ENTRY; END=SPW_PROFILE_ID_RX_ZC_RELEASE_API_RETURN ;;
    *) return 1 ;;
  esac
}

cleanup_openocd() {
  if [[ -n "${OPENOCD_PID:-}" ]] && kill -0 "$OPENOCD_PID" >/dev/null 2>&1; then
    kill "$OPENOCD_PID" >/dev/null 2>&1 || true
    wait "$OPENOCD_PID" >/dev/null 2>&1 || true
  fi
  OPENOCD_PID=""
}
trap cleanup_openocd EXIT INT TERM

git_sha="$(git -C "$ROOT_DIR" rev-parse HEAD)"
git_short="$(git -C "$ROOT_DIR" rev-parse --short=8 HEAD)"
timestamp="$(date -u +%Y%m%dT%H%M%SZ)"
result_name="stm32h755-${timestamp}-${git_short}"
output_dir="$OUTPUT_ROOT/$result_name"
campaign_build="$BUILD_ROOT/$result_name"
mkdir -p "$output_dir/cases" "$output_dir/logs"
rm -rf -- "$campaign_build"
mkdir -p "$campaign_build"
compiler="$(arm-none-eabi-gcc --version | head -n 1)"

printf 'SpWKit STM32H755 physical profiling campaign\n' >&2
printf '  commit: %s\n' "$git_sha" >&2
printf '  STM32CubeH7: %s\n' "$CUBE_SHA" >&2
printf '  build: Release, clean per probe pair, serial\n' >&2
printf '  counter: Cortex-M7 DWT CYCCNT at reset-clock 64 MHz\n' >&2
printf '  payloads: 0 1 8 64 256 bytes\n' >&2
printf '  cases: %s\n' "${selected_cases[*]}" >&2
printf '  results: %s\n' "$output_dir" >&2

for case_name in "${selected_cases[@]}"; do
  case_config "$case_name"
  case_root="$campaign_build/$case_name"
  spw_build="$case_root/spwkit"
  install_dir="$case_root/install"
  fw_build="$case_root/firmware"
  elf="$fw_build/spwkit_stm32h755_profile.elf"
  build_log="$output_dir/logs/${case_name}-build.log"
  openocd_log="$output_dir/logs/${case_name}-openocd.log"
  gdb_log="$output_dir/logs/${case_name}-gdb.log"
  json_out="$output_dir/cases/${case_name}.json"

  printf '\n[STM32 profile] %s: %s -> %s\n' "$case_name" "$START" "$END" >&2
  rm -rf -- "$case_root"
  mkdir -p "$case_root"

  {
    cmake -S "$ROOT_DIR" -B "$spw_build" \
      -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/cmake/toolchains/arm-none-eabi-cortex-m7.cmake" \
      -DCMAKE_INSTALL_PREFIX="$install_dir" \
      -DCMAKE_BUILD_TYPE=Release \
      -DSPWKIT_ENABLE_HEAP=OFF \
      -DSPWKIT_ENABLE_PROFILING=ON \
      -DSPWKIT_PROFILE_START="$START" \
      -DSPWKIT_PROFILE_END="$END" \
      -DSPWKIT_BUILD_TESTS=OFF \
      -DSPWKIT_BUILD_CPP_TESTS=OFF \
      -DSPWKIT_BUILD_EXAMPLES=OFF \
      -DSPWKIT_BUILD_CPP_EXAMPLES=OFF \
      -DSPWKIT_BUILD_SIMULATOR=OFF \
      -DSPWKIT_BUILD_UDP=OFF \
      -DSPWKIT_BUILD_DEVICE=OFF
    cmake --build "$spw_build" --parallel 2
    cmake --install "$spw_build"

    cmake -S "$ROOT_DIR/integrations/stm32h755_dma" -B "$fw_build" \
      -DCMAKE_TOOLCHAIN_FILE="$ROOT_DIR/cmake/toolchains/arm-none-eabi-cortex-m7.cmake" \
      -DCMAKE_PREFIX_PATH="$install_dir" \
      -DCMAKE_BUILD_TYPE=Release \
      -DSTM32_CUBE_H7_DIR="$STM32_CUBE_H7_DIR" \
      -DSPWKIT_STM32_PROFILE_FIRMWARE=ON \
      -DSPWKIT_SOURCE_DIR="$ROOT_DIR" \
      -DSPWKIT_STM32_PROFILE_START="$START" \
      -DSPWKIT_STM32_PROFILE_END="$END" \
      -DSPWKIT_STM32_PROFILE_CASE="$CASE_ID" \
      -DSPWKIT_STM32_PROFILE_WARMUP="$WARMUP" \
      -DSPWKIT_STM32_PROFILE_ITERATIONS="$ITERATIONS"
    cmake --build "$fw_build" --target spwkit_stm32h755_profile --parallel 2
  } >"$build_log" 2>&1 || {
    cat "$build_log" >&2
    echo "Build failed for $case_name; partial results preserved at $output_dir" >&2
    exit 1
  }

  [[ -s "$elf" ]] || { echo "Profile ELF missing: $elf" >&2; exit 1; }
  arm-none-eabi-nm -g "$elf" | grep -q 'g_stm32h755_spwkit_profile' || {
    echo "Profile ELF lacks g_stm32h755_spwkit_profile" >&2; exit 1;
  }

  cleanup_openocd
  openocd -s "$OPENOCD_SCRIPTS" \
    -f "$ROOT_DIR/scripts/openocd_h755.cfg" \
    -c "init; reset halt" >"$openocd_log" 2>&1 &
  OPENOCD_PID=$!
  for ((attempt = 0; attempt < 100; ++attempt)); do
    grep -q "Listening on port 3333 for gdb connections" "$openocd_log" 2>/dev/null && break
    if ! kill -0 "$OPENOCD_PID" >/dev/null 2>&1; then
      cat "$openocd_log" >&2
      echo "OpenOCD exited before GDB server ready" >&2
      exit 1
    fi
    sleep 0.1
  done
  grep -q "Listening on port 3333 for gdb connections" "$openocd_log" || {
    cat "$openocd_log" >&2; echo "OpenOCD GDB server timeout" >&2; exit 1;
  }

  set +e
  timeout "${DEBUG_TIMEOUT}s" "$GDB_BIN" -q "$elf" -batch \
    -x "$ROOT_DIR/scripts/gdb/stm32h755_profile.gdb" 2>&1 | tee "$gdb_log"
  gdb_rc=${PIPESTATUS[0]}
  set -e
  cleanup_openocd
  if (( gdb_rc != 0 )) || ! grep -q '^RESULT: PASS$' "$gdb_log"; then
    echo "Physical profile case $case_name failed; logs preserved at $output_dir" >&2
    exit "${gdb_rc:-1}"
  fi

  python3 "$ROOT_DIR/scripts/extract_stm32h755_profile.py" "$gdb_log" \
    --case "$case_name" --start "$START" --end "$END" \
    --git-sha "$git_sha" --cube-sha "$CUBE_SHA" --compiler "$compiler" \
    --output "$json_out"
done

export SPWKIT_STM32_RESULT_DIR="$output_dir"
export SPWKIT_STM32_RESULT_NAME="$result_name"
export SPWKIT_STM32_GIT_SHA="$git_sha"
export SPWKIT_STM32_CUBE_SHA="$CUBE_SHA"
export SPWKIT_STM32_COMPILER="$compiler"
export SPWKIT_STM32_CASES="${selected_cases[*]}"
export SPWKIT_STM32_WARMUP="$WARMUP"
export SPWKIT_STM32_ITERATIONS="$ITERATIONS"
python3 - <<'PY'
import json
import os
from pathlib import Path
root = Path(os.environ['SPWKIT_STM32_RESULT_DIR'])
meta = {
    'schema': 'spwkit.profile.stm32h755-campaign.v1',
    'result_type': 'physical-board',
    'result_directory_name': os.environ['SPWKIT_STM32_RESULT_NAME'],
    'git_sha': os.environ['SPWKIT_STM32_GIT_SHA'],
    'stm32cubeh7_sha': os.environ['SPWKIT_STM32_CUBE_SHA'],
    'compiler': os.environ['SPWKIT_STM32_COMPILER'],
    'board': 'NUCLEO-H755ZI-Q',
    'mcu': 'STM32H755ZI',
    'core': 'Cortex-M7',
    'core_hz': 64000000,
    'counter': 'DWT_CYCCNT',
    'build_type': 'Release',
    'clean_rebuild_per_case': True,
    'serial_execution': True,
    'payloads': [0, 1, 8, 64, 256],
    'warmup': int(os.environ['SPWKIT_STM32_WARMUP']),
    'iterations': int(os.environ['SPWKIT_STM32_ITERATIONS']),
    'cases': os.environ['SPWKIT_STM32_CASES'].split(),
    'counter_floor_subtracted': False,
    'scope': 'DMA2 memory-to-memory provider; not SpaceWire PHY/link timing',
}
(root / 'campaign.json').write_text(json.dumps(meta, indent=2) + '\n')
PY

printf '\n================ STM32H755 PROFILING RESULTS ================\n' >&2
python3 "$ROOT_DIR/scripts/summarize_stm32h755_profile.py" "$output_dir" | tee "$output_dir/summary.console.txt" >&2
printf '==============================================================\n' >&2
archive="${output_dir}.tar"
tar -cf "$archive" -C "$(dirname "$output_dir")" "$(basename "$output_dir")"
printf 'Human summary : %s/summary.txt\n' "$output_dir" >&2
printf 'Machine data  : %s/cases/*.json\n' "$output_dir" >&2
printf 'Archive       : %s\n' "$archive" >&2
printf '%s\n' "$output_dir"
''')
campaign_path.chmod(0o755)


# ---------------------------------------------------------------------------
# README physical profiling section.
# ---------------------------------------------------------------------------
readme_path = ROOT / "integrations/stm32h755_dma/README.md"
readme = readme_path.read_text()
profile_docs = r'''
## Physical DWT profiling

The Debug correctness firmware above and the performance firmware are intentionally separate. Physical profiling uses an optimized Release build, DWT `CYCCNT`, fixed/no-heap sample storage, and a clean rebuild/flash for every compiled probe pair.

With the NUCLEO-H755ZI-Q connected, run:

```bash
scripts/stm32h755_profile_campaign.sh --stm32h7-root /tmp/STM32CubeH7
```

The first physical sweep stays within the existing provider's real 256-byte DMA buffer capacity:

```text
0 1 8 64 256 bytes
```

The campaign measures these compiled ranges serially:

- copied TX: public API entry -> provider DMA submission;
- copied RX: provider data-ready -> public API return;
- zero-copy TX acquire: API entry -> API return;
- zero-copy TX submit: API entry -> provider DMA submission;
- zero-copy TX reclaim: provider completion boundary -> API return;
- zero-copy TX release: API entry -> API return;
- zero-copy RX acquire: provider data-ready -> API return;
- zero-copy RX release: API entry -> API return.

The copied-TX configuration also records isolated Cortex-M7 D-cache clean and invalidate costs for the same payload sizes. Every configuration records its own back-to-back DWT read floor; calibration is diagnostic and is never subtracted from measured intervals.

Results are written under `build/profile-results/stm32h755-<UTC>-<commit>/` and the runner also creates a `.tar` archive beside the result directory. `summary.txt` contains rounded integral cycle values while each `cases/*.json` file retains raw DWT samples and full-precision derived statistics.

These measurements characterize the SpWKit software/provider/DMA/cache path on STM32H755. DMA2 is used as a concrete hardware-backed provider boundary, but there is still no SpaceWire controller, codec, PHY, cable, or link serialization in this fixture. Do not report these values as SpaceWire link latency.

'''
readme = replace_once(readme, "## Acceptance record for #119\n", profile_docs + "## Acceptance record for #119\n", "README profiling section")
readme_path.write_text(readme)


# ---------------------------------------------------------------------------
# Hosted workflow validates mechanics only: scripts, parser, and one Release
# cross-build using the same paired probes. It cannot produce physical timing.
# ---------------------------------------------------------------------------
workflow_path = ROOT / ".github/workflows/stm32h755-dma.yml"
workflow = workflow_path.read_text()
workflow = replace_once(
    workflow,
    "      - 'scripts/stm32h755_board_test.sh'\n      - 'scripts/openocd_h755.cfg'\n      - 'scripts/gdb/stm32h755_board_test.gdb'\n",
    "      - 'scripts/stm32h755_board_test.sh'\n      - 'scripts/stm32h755_profile_campaign.sh'\n      - 'scripts/extract_stm32h755_profile.py'\n      - 'scripts/summarize_stm32h755_profile.py'\n      - 'scripts/openocd_h755.cfg'\n      - 'scripts/gdb/stm32h755_board_test.gdb'\n      - 'scripts/gdb/stm32h755_profile.gdb'\n",
    "workflow push paths",
)
workflow = replace_once(
    workflow,
    "      - 'scripts/stm32h755_board_test.sh'\n      - 'scripts/openocd_h755.cfg'\n      - 'scripts/gdb/stm32h755_board_test.gdb'\n",
    "      - 'scripts/stm32h755_board_test.sh'\n      - 'scripts/stm32h755_profile_campaign.sh'\n      - 'scripts/extract_stm32h755_profile.py'\n      - 'scripts/summarize_stm32h755_profile.py'\n      - 'scripts/openocd_h755.cfg'\n      - 'scripts/gdb/stm32h755_board_test.gdb'\n      - 'scripts/gdb/stm32h755_profile.gdb'\n",
    "workflow PR paths",
)
workflow = replace_once(
    workflow,
    "          test -x scripts/stm32h755_board_test.sh\n          bash -n scripts/stm32h755_board_test.sh\n",
    "          test -x scripts/stm32h755_board_test.sh\n          test -x scripts/stm32h755_profile_campaign.sh\n          bash -n scripts/stm32h755_board_test.sh\n          bash -n scripts/stm32h755_profile_campaign.sh\n          python3 -m py_compile scripts/extract_stm32h755_profile.py scripts/summarize_stm32h755_profile.py\n",
    "workflow syntax validation",
)
workflow += r'''

      - name: Cross-build representative Release profiling firmware
        shell: bash
        run: |
          set -euo pipefail
          cmake -S . -B build/stm32-profile-spwkit \
            -DCMAKE_TOOLCHAIN_FILE="${GITHUB_WORKSPACE}/cmake/toolchains/arm-none-eabi-cortex-m7.cmake" \
            -DCMAKE_INSTALL_PREFIX="${GITHUB_WORKSPACE}/build/stm32-profile-install" \
            -DCMAKE_BUILD_TYPE=Release \
            -DSPWKIT_ENABLE_HEAP=OFF \
            -DSPWKIT_ENABLE_PROFILING=ON \
            -DSPWKIT_PROFILE_START=SPW_PROFILE_ID_TX_API_ENTRY \
            -DSPWKIT_PROFILE_END=SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY \
            -DSPWKIT_BUILD_TESTS=OFF \
            -DSPWKIT_BUILD_CPP_TESTS=OFF \
            -DSPWKIT_BUILD_EXAMPLES=OFF \
            -DSPWKIT_BUILD_CPP_EXAMPLES=OFF \
            -DSPWKIT_BUILD_SIMULATOR=OFF \
            -DSPWKIT_BUILD_UDP=OFF \
            -DSPWKIT_BUILD_DEVICE=OFF
          cmake --build build/stm32-profile-spwkit --parallel 2
          cmake --install build/stm32-profile-spwkit
          cmake -S integrations/stm32h755_dma -B build/stm32h755-profile \
            -DCMAKE_TOOLCHAIN_FILE="${GITHUB_WORKSPACE}/cmake/toolchains/arm-none-eabi-cortex-m7.cmake" \
            -DCMAKE_PREFIX_PATH="${GITHUB_WORKSPACE}/build/stm32-profile-install" \
            -DCMAKE_BUILD_TYPE=Release \
            -DSTM32_CUBE_H7_DIR="${STM32_CUBE_H7_DIR}" \
            -DSPWKIT_STM32_PROFILE_FIRMWARE=ON \
            -DSPWKIT_SOURCE_DIR="${GITHUB_WORKSPACE}" \
            -DSPWKIT_STM32_PROFILE_START=SPW_PROFILE_ID_TX_API_ENTRY \
            -DSPWKIT_STM32_PROFILE_END=SPW_PROFILE_ID_TX_PROVIDER_BOUNDARY \
            -DSPWKIT_STM32_PROFILE_CASE=1 \
            -DSPWKIT_STM32_PROFILE_WARMUP=2 \
            -DSPWKIT_STM32_PROFILE_ITERATIONS=4
          cmake --build build/stm32h755-profile --target spwkit_stm32h755_profile --parallel 2
          elf="build/stm32h755-profile/spwkit_stm32h755_profile.elf"
          test -s "${elf}"
          arm-none-eabi-nm -g "${elf}" | grep -q 'g_stm32h755_spwkit_profile'
          if arm-none-eabi-nm -u "${elf}" | grep -Eq '(^|[[:space:]])(malloc|calloc|realloc|free|printf|puts|write|read)$'; then
            echo 'Unexpected hosted runtime symbol in STM32H755 profile firmware' >&2
            arm-none-eabi-nm -u "${elf}" >&2
            exit 1
          fi
          arm-none-eabi-size "${elf}"
'''
workflow_path.write_text(workflow)

print("STM32H755 physical profiling implementation staged")
