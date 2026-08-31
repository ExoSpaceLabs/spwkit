// SPDX-License-Identifier: Apache-2.0

#include <spwkit/driver.h>
#include <spwkit/spwkit.h>

#include "stm32h755xx.h"

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

#define STM32_DMA_PACKET_CAPACITY 256u
#define STM32_WORKSPACE_BYTES 4096u
#define STM32_DMA_TIMEOUT_SPINS 10000000u
#define STM32_EVIDENCE_MAGIC UINT32_C(0x53505736) /* "SPW6" */

extern uint32_t __StackTop;
extern uint32_t __data_load__;
extern uint32_t __data_start__;
extern uint32_t __data_end__;
extern uint32_t __bss_start__;
extern uint32_t __bss_end__;
extern uint32_t __dma_buffer_start__;
extern uint32_t __dma_buffer_end__;

void Reset_Handler(void);

__attribute__((section(".isr_vector"), used, aligned(256)))
const uintptr_t g_stm32h755_vector_table[16] = {
    (uintptr_t)&__StackTop,
    (uintptr_t)&Reset_Handler,
    0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
};

typedef enum stm32_dma_owner {
    STM32_DMA_FREE = 0,
    STM32_DMA_TX_APP,
    STM32_DMA_TX_DEVICE,
    STM32_DMA_TX_COMPLETE,
    STM32_DMA_RX_DEVICE,
    STM32_DMA_RX_READY,
    STM32_DMA_RX_APP
} stm32_dma_owner_t;

typedef struct stm32_driver_state {
    spw_link_state_t link_state;
    stm32_dma_owner_t tx_owner;
    stm32_dma_owner_t rx_owner;
    size_t transfer_length;
    spw_terminator_t transfer_terminator;
    spw_statistics_t statistics;
    uint32_t sync_to_device;
    uint32_t sync_from_device;
    uint32_t dma_transfers;
} stm32_driver_state_t;

typedef struct stm32_evidence {
    uint32_t magic;
    volatile uint32_t phase;
    volatile uint32_t result;
    volatile uint32_t sync_to_device;
    volatile uint32_t sync_from_device;
    volatile uint32_t dma_transfers;
    volatile uint32_t tx_packets;
    volatile uint32_t rx_packets;
} stm32_evidence_t;

/* Public debugger/HIL evidence symbol. result=0 means the firmware completed
 * the MCU driver contract. Values >= 0x100 identify the failing phase. */
volatile stm32_evidence_t g_stm32h755_spwkit_evidence = {
    STM32_EVIDENCE_MAGIC,
    0u,
    UINT32_C(0xffffffff),
    0u, 0u, 0u, 0u, 0u
};

static alignas(max_align_t) uint8_t g_workspace[STM32_WORKSPACE_BYTES];
static stm32_driver_state_t g_driver;

/* DMA-owned storage is deliberately outside AXI workspace/state and outside
 * TCM. D2 SRAM is reachable by DMA2 on STM32H755. */
__attribute__((section(".dma_buffer"), aligned(32), used))
static uint8_t g_dma_tx[STM32_DMA_PACKET_CAPACITY];
__attribute__((section(".dma_buffer"), aligned(32), used))
static uint8_t g_dma_rx[STM32_DMA_PACKET_CAPACITY];

static const spw_driver_buffer_token_t TX_TOKEN = UINT64_C(0x75510001);
static const spw_driver_buffer_token_t RX_TOKEN = UINT64_C(0x75520001);

static void memory_init(void) {
    uint32_t* source = &__data_load__;
    uint32_t* destination = &__data_start__;
    while (destination < &__data_end__) {
        *destination++ = *source++;
    }

    destination = &__bss_start__;
    while (destination < &__bss_end__) {
        *destination++ = 0u;
    }

    destination = &__dma_buffer_start__;
    while (destination < &__dma_buffer_end__) {
        *destination++ = 0u;
    }
}

static void bytes_copy(uint8_t* destination, const uint8_t* source, size_t size) {
    size_t index;
    for (index = 0u; index < size; ++index) {
        destination[index] = source[index];
    }
}

static int bytes_equal(const uint8_t* lhs, const uint8_t* rhs, size_t size) {
    size_t index;
    for (index = 0u; index < size; ++index) {
        if (lhs[index] != rhs[index]) {
            return 0;
        }
    }
    return 1;
}

static uintptr_t cache_line_start(const void* pointer) {
    return ((uintptr_t)pointer) & ~(uintptr_t)31u;
}

static int32_t cache_line_span(const void* pointer, size_t size) {
    const uintptr_t start = cache_line_start(pointer);
    const uintptr_t end = ((uintptr_t)pointer + size + 31u) & ~(uintptr_t)31u;
    return (int32_t)(end - start);
}

static void clean_cache(const void* pointer, size_t size) {
    if (size != 0u) {
        SCB_CleanDCache_by_Addr((void*)cache_line_start(pointer),
                                cache_line_span(pointer, size));
        __DSB();
    }
}

static void invalidate_cache(const void* pointer, size_t size) {
    if (size != 0u) {
        SCB_InvalidateDCache_by_Addr((void*)cache_line_start(pointer),
                                     cache_line_span(pointer, size));
        __DSB();
    }
}

static void dma_clear_stream0_flags(void) {
    DMA2->LIFCR = DMA_LIFCR_CFEIF0 |
                  DMA_LIFCR_CDMEIF0 |
                  DMA_LIFCR_CTEIF0 |
                  DMA_LIFCR_CHTIF0 |
                  DMA_LIFCR_CTCIF0;
}

static int dma_has_error(void) {
    return (DMA2->LISR & (DMA_LISR_TEIF0 | DMA_LISR_DMEIF0 | DMA_LISR_FEIF0)) != 0u;
}

static int dma_complete(void) {
    return (DMA2->LISR & DMA_LISR_TCIF0) != 0u;
}

static int dma_wait_complete(void) {
    uint32_t spins = STM32_DMA_TIMEOUT_SPINS;
    while (!dma_complete()) {
        if (dma_has_error() || spins-- == 0u) {
            return 0;
        }
    }
    return 1;
}

static int dma_start_copy(size_t length) {
    if (length > STM32_DMA_PACKET_CAPACITY) {
        return 0;
    }
    if (length == 0u) {
        return 1;
    }

    DMA2_Stream0->CR &= ~DMA_SxCR_EN;
    while ((DMA2_Stream0->CR & DMA_SxCR_EN) != 0u) {
    }

    dma_clear_stream0_flags();
    DMA2_Stream0->PAR = (uint32_t)(uintptr_t)g_dma_tx;
    DMA2_Stream0->M0AR = (uint32_t)(uintptr_t)g_dma_rx;
    DMA2_Stream0->NDTR = (uint32_t)length;
    DMA2_Stream0->FCR = 0u;
    DMA2_Stream0->CR = DMA_SxCR_DIR_1 |
                       DMA_SxCR_PINC |
                       DMA_SxCR_MINC |
                       DMA_SxCR_PL_1;
    __DSB();
    DMA2_Stream0->CR |= DMA_SxCR_EN;
    return 1;
}

static void driver_poll_completion(stm32_driver_state_t* state) {
    if (state->tx_owner != STM32_DMA_TX_DEVICE || !dma_complete()) {
        return;
    }

    DMA2_Stream0->CR &= ~DMA_SxCR_EN;
    dma_clear_stream0_flags();
    state->tx_owner = STM32_DMA_TX_COMPLETE;
    state->rx_owner = STM32_DMA_RX_READY;
    ++state->dma_transfers;
}

static spw_result_t driver_start(void* raw) {
    stm32_driver_state_t* state = (stm32_driver_state_t*)raw;
    if (state == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    state->link_state = SPW_LINK_RUN;
    return SPW_OK;
}

static spw_result_t driver_stop(void* raw) {
    stm32_driver_state_t* state = (stm32_driver_state_t*)raw;
    if (state == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    state->link_state = SPW_LINK_READY;
    return SPW_OK;
}

static spw_result_t driver_reset(void* raw) {
    stm32_driver_state_t* state = (stm32_driver_state_t*)raw;
    if (state == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    DMA2_Stream0->CR &= ~DMA_SxCR_EN;
    dma_clear_stream0_flags();
    state->link_state = SPW_LINK_ERROR_RESET;
    state->tx_owner = STM32_DMA_FREE;
    state->rx_owner = STM32_DMA_FREE;
    state->transfer_length = 0u;
    return SPW_OK;
}

static spw_result_t driver_get_link_state(const void* raw,
                                          spw_link_state_t* out_state) {
    const stm32_driver_state_t* state = (const stm32_driver_state_t*)raw;
    if (state == NULL || out_state == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_state = state->link_state;
    return SPW_OK;
}

static spw_result_t driver_get_capabilities(const void* raw,
                                            spw_capabilities_t* out_caps) {
    (void)raw;
    if (out_caps == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_caps = (spw_capabilities_t){0};
    out_caps->bits = SPW_CAP_EEP |
                     SPW_CAP_LINK_CONTROL |
                     SPW_CAP_STATISTICS |
                     SPW_CAP_ZERO_COPY;
    out_caps->max_packet_size = STM32_DMA_PACKET_CAPACITY;
    out_caps->tx_queue_depth = 1u;
    out_caps->rx_queue_depth = 1u;
    out_caps->buffer_alignment = 32u;
    return SPW_OK;
}

static spw_result_t driver_send(void* raw,
                                const spw_packet_t* packet,
                                spw_timeout_us_t timeout_us) {
    stm32_driver_state_t* state = (stm32_driver_state_t*)raw;
    (void)timeout_us;

    if (state == NULL || packet == NULL ||
        (packet->length != 0u && packet->data == NULL)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (state->link_state != SPW_LINK_RUN) {
        return SPW_ERR_INVALID_STATE;
    }
    if (state->tx_owner != STM32_DMA_FREE || state->rx_owner != STM32_DMA_FREE) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }
    if (packet->length > sizeof(g_dma_tx)) {
        return SPW_ERR_INVALID_PACKET;
    }

    bytes_copy(g_dma_tx, packet->data, packet->length);
    clean_cache(g_dma_tx, packet->length);
    invalidate_cache(g_dma_rx, packet->length);
    state->transfer_length = packet->length;
    state->transfer_terminator = packet->terminator;
    state->tx_owner = STM32_DMA_TX_DEVICE;
    state->rx_owner = STM32_DMA_RX_DEVICE;

    if (!dma_start_copy(packet->length) ||
        (packet->length != 0u && !dma_wait_complete())) {
        state->tx_owner = STM32_DMA_FREE;
        state->rx_owner = STM32_DMA_FREE;
        ++state->statistics.link_errors;
        return SPW_ERR_BACKEND;
    }
    if (packet->length == 0u) {
        state->tx_owner = STM32_DMA_TX_COMPLETE;
        state->rx_owner = STM32_DMA_RX_READY;
        ++state->dma_transfers;
    } else {
        driver_poll_completion(state);
    }
    invalidate_cache(g_dma_rx, packet->length);
    ++state->statistics.tx_packets;
    state->statistics.tx_bytes += packet->length;
    return SPW_OK;
}

static spw_result_t driver_receive(void* raw,
                                   spw_packet_t* packet,
                                   spw_timeout_us_t timeout_us) {
    stm32_driver_state_t* state = (stm32_driver_state_t*)raw;
    (void)timeout_us;

    if (state == NULL || packet == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    driver_poll_completion(state);
    if (state->rx_owner != STM32_DMA_RX_READY) {
        return SPW_ERR_TIMEOUT;
    }
    if (packet->capacity < state->transfer_length) {
        packet->length = state->transfer_length;
        packet->terminator = state->transfer_terminator;
        return SPW_ERR_BUFFER_TOO_SMALL;
    }
    if (state->transfer_length != 0u && packet->data == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }

    invalidate_cache(g_dma_rx, state->transfer_length);
    bytes_copy(packet->data, g_dma_rx, state->transfer_length);
    packet->length = state->transfer_length;
    packet->terminator = state->transfer_terminator;
    state->rx_owner = STM32_DMA_FREE;
    state->tx_owner = STM32_DMA_FREE;
    ++state->statistics.rx_packets;
    state->statistics.rx_bytes += packet->length;
    return SPW_OK;
}

static spw_result_t driver_get_statistics(const void* raw,
                                          spw_statistics_t* out_statistics) {
    const stm32_driver_state_t* state = (const stm32_driver_state_t*)raw;
    if (state == NULL || out_statistics == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_statistics = state->statistics;
    return SPW_OK;
}

static spw_result_t driver_clear_statistics(void* raw) {
    stm32_driver_state_t* state = (stm32_driver_state_t*)raw;
    if (state == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    state->statistics = (spw_statistics_t){0};
    return SPW_OK;
}

static spw_driver_buffer_t make_tx_descriptor(const stm32_driver_state_t* state) {
    spw_driver_buffer_t descriptor = {
        g_dma_tx,
        state->transfer_length,
        sizeof(g_dma_tx),
        state->transfer_terminator,
        TX_TOKEN
    };
    return descriptor;
}

static spw_driver_buffer_t make_rx_descriptor(const stm32_driver_state_t* state) {
    spw_driver_buffer_t descriptor = {
        g_dma_rx,
        state->transfer_length,
        sizeof(g_dma_rx),
        state->transfer_terminator,
        RX_TOKEN
    };
    return descriptor;
}

static spw_result_t driver_acquire_tx(void* raw,
                                      size_t min_capacity,
                                      spw_timeout_us_t timeout_us,
                                      spw_driver_buffer_t* out_buffer) {
    stm32_driver_state_t* state = (stm32_driver_state_t*)raw;
    (void)timeout_us;
    if (state == NULL || out_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (min_capacity > sizeof(g_dma_tx)) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }
    if (state->tx_owner != STM32_DMA_FREE || state->rx_owner != STM32_DMA_FREE) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }
    state->tx_owner = STM32_DMA_TX_APP;
    state->transfer_length = 0u;
    state->transfer_terminator = SPW_TERMINATOR_EOP;
    *out_buffer = make_tx_descriptor(state);
    return SPW_OK;
}

static spw_result_t driver_submit_tx(void* raw,
                                     const spw_driver_buffer_t* buffer,
                                     spw_timeout_us_t timeout_us) {
    stm32_driver_state_t* state = (stm32_driver_state_t*)raw;
    (void)timeout_us;
    if (state == NULL || buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (state->tx_owner != STM32_DMA_TX_APP ||
        buffer->token != TX_TOKEN || buffer->data != g_dma_tx ||
        buffer->capacity != sizeof(g_dma_tx) || buffer->length > buffer->capacity) {
        return SPW_ERR_BACKEND;
    }

    state->transfer_length = buffer->length;
    state->transfer_terminator = buffer->terminator;
    state->tx_owner = STM32_DMA_TX_DEVICE;
    state->rx_owner = STM32_DMA_RX_DEVICE;
    invalidate_cache(g_dma_rx, buffer->length);
    if (!dma_start_copy(buffer->length)) {
        state->tx_owner = STM32_DMA_TX_APP;
        state->rx_owner = STM32_DMA_FREE;
        return SPW_ERR_BACKEND;
    }
    if (buffer->length == 0u) {
        state->tx_owner = STM32_DMA_TX_COMPLETE;
        state->rx_owner = STM32_DMA_RX_READY;
        ++state->dma_transfers;
    }
    ++state->statistics.tx_packets;
    state->statistics.tx_bytes += buffer->length;
    return SPW_OK;
}

static spw_result_t driver_reclaim_tx(void* raw,
                                      spw_timeout_us_t timeout_us,
                                      spw_driver_buffer_t* out_buffer) {
    stm32_driver_state_t* state = (stm32_driver_state_t*)raw;
    uint32_t spins = STM32_DMA_TIMEOUT_SPINS;
    (void)timeout_us;
    if (state == NULL || out_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    while (state->tx_owner == STM32_DMA_TX_DEVICE && spins-- != 0u) {
        if (dma_has_error()) {
            return SPW_ERR_BACKEND;
        }
        driver_poll_completion(state);
    }
    if (state->tx_owner != STM32_DMA_TX_COMPLETE) {
        return SPW_ERR_TIMEOUT;
    }
    state->tx_owner = STM32_DMA_TX_APP;
    *out_buffer = make_tx_descriptor(state);
    return SPW_OK;
}

static spw_result_t driver_release_tx(void* raw,
                                      const spw_driver_buffer_t* buffer) {
    stm32_driver_state_t* state = (stm32_driver_state_t*)raw;
    if (state == NULL || buffer == NULL ||
        state->tx_owner != STM32_DMA_TX_APP ||
        buffer->token != TX_TOKEN || buffer->data != g_dma_tx) {
        return SPW_ERR_BACKEND;
    }
    state->tx_owner = STM32_DMA_FREE;
    return SPW_OK;
}

static spw_result_t driver_acquire_rx(void* raw,
                                      spw_timeout_us_t timeout_us,
                                      spw_driver_buffer_t* out_buffer) {
    stm32_driver_state_t* state = (stm32_driver_state_t*)raw;
    uint32_t spins = STM32_DMA_TIMEOUT_SPINS;
    (void)timeout_us;
    if (state == NULL || out_buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    while (state->rx_owner == STM32_DMA_RX_DEVICE && spins-- != 0u) {
        if (dma_has_error()) {
            return SPW_ERR_BACKEND;
        }
        driver_poll_completion(state);
    }
    if (state->rx_owner != STM32_DMA_RX_READY) {
        return SPW_ERR_TIMEOUT;
    }
    state->rx_owner = STM32_DMA_RX_APP;
    ++state->statistics.rx_packets;
    state->statistics.rx_bytes += state->transfer_length;
    *out_buffer = make_rx_descriptor(state);
    return SPW_OK;
}

static spw_result_t driver_release_rx(void* raw,
                                      const spw_driver_buffer_t* buffer) {
    stm32_driver_state_t* state = (stm32_driver_state_t*)raw;
    if (state == NULL || buffer == NULL ||
        state->rx_owner != STM32_DMA_RX_APP ||
        buffer->token != RX_TOKEN || buffer->data != g_dma_rx) {
        return SPW_ERR_BACKEND;
    }
    state->rx_owner = STM32_DMA_FREE;
    return SPW_OK;
}

static spw_result_t driver_sync(void* raw,
                                const spw_driver_buffer_t* buffer,
                                spw_driver_sync_direction_t direction) {
    stm32_driver_state_t* state = (stm32_driver_state_t*)raw;
    if (state == NULL || buffer == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (direction == SPW_DRIVER_SYNC_TO_DEVICE) {
        clean_cache(buffer->data, buffer->length);
        ++state->sync_to_device;
        return SPW_OK;
    }
    if (direction == SPW_DRIVER_SYNC_FROM_DEVICE) {
        invalidate_cache(buffer->data, buffer->length);
        ++state->sync_from_device;
        return SPW_OK;
    }
    return SPW_ERR_INVALID_ARGUMENT;
}

static const spw_driver_ops_t STM32_DRIVER_OPS = {
    sizeof(spw_driver_ops_t), SPW_DRIVER_OPS_VERSION,
    driver_start, driver_stop, driver_reset,
    driver_get_link_state, driver_get_capabilities,
    driver_send, driver_receive,
    NULL, NULL,
    driver_get_statistics, driver_clear_statistics,
    NULL,
    driver_acquire_tx, driver_submit_tx,
    driver_reclaim_tx, driver_release_tx,
    driver_acquire_rx, driver_release_rx,
    driver_sync
};

static int run_contract(void) {
    static const uint8_t copied_payload[] = {
        0x53u, 0x70u, 0x57u, 0x4bu, 0x2du, 0x48u, 0x37u, 0x35u, 0x35u
    };
    static const uint8_t zero_copy_payload[] = {
        0x44u, 0x4du, 0x41u, 0x2du, 0x5au, 0x45u, 0x52u, 0x4fu,
        0x2du, 0x43u, 0x4fu, 0x50u, 0x59u
    };
    spw_driver_config_t driver_config =
        SPW_DRIVER_CONFIG_INITIALIZER(&STM32_DRIVER_OPS, &g_driver);
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DRIVER);
    spw_port_workspace_requirements_t requirements = {0u, 0u};
    spw_port_t* port = NULL;
    uint8_t copied_rx[sizeof(copied_payload)] = {0u};
    spw_packet_t copied_tx = {
        (uint8_t*)copied_payload,
        sizeof(copied_payload),
        sizeof(copied_payload),
        SPW_TERMINATOR_EEP
    };
    spw_packet_t copied_packet_rx = {
        copied_rx,
        0u,
        sizeof(copied_rx),
        SPW_TERMINATOR_EOP
    };
    spw_buffer_t* tx_buffer = NULL;
    spw_buffer_t* rx_buffer = NULL;
    spw_buffer_t* reclaimed = NULL;
    spw_buffer_view_t tx_view = {0};
    spw_buffer_view_t rx_view = {0};
    spw_buffer_view_t reclaimed_view = {0};
    spw_statistics_t statistics = {0};
    uint8_t* submitted_pointer = NULL;

    driver_config.tx_buffer_slots = 1u;
    driver_config.rx_buffer_slots = 1u;
    config.backend_config = &driver_config;
    config.backend_config_size = sizeof(driver_config);

    g_stm32h755_spwkit_evidence.phase = 1u;
    if (spw_port_workspace_requirements(&config, &requirements) != SPW_OK ||
        requirements.size > sizeof(g_workspace) ||
        requirements.alignment > alignof(max_align_t)) {
        return 0x101;
    }
    if (spw_port_open_in_place(&config, g_workspace, sizeof(g_workspace), &port) != SPW_OK ||
        port == NULL || spw_port_start(port) != SPW_OK) {
        return 0x102;
    }

    g_stm32h755_spwkit_evidence.phase = 2u;
    if (spw_port_send(port, &copied_tx, 50000u) != SPW_OK ||
        spw_port_receive(port, &copied_packet_rx, 50000u) != SPW_OK ||
        copied_packet_rx.length != sizeof(copied_payload) ||
        copied_packet_rx.terminator != SPW_TERMINATOR_EEP ||
        !bytes_equal(copied_payload, copied_rx, sizeof(copied_payload))) {
        return 0x201;
    }

    g_stm32h755_spwkit_evidence.phase = 3u;
    if (spw_port_acquire_tx_buffer(port, sizeof(zero_copy_payload), 50000u, &tx_buffer) != SPW_OK ||
        tx_buffer == NULL ||
        spw_buffer_get_view(tx_buffer, &tx_view) != SPW_OK ||
        tx_view.data != g_dma_tx || tx_view.capacity < sizeof(zero_copy_payload)) {
        return 0x301;
    }
    bytes_copy(tx_view.data, zero_copy_payload, sizeof(zero_copy_payload));
    submitted_pointer = tx_view.data;
    if (spw_buffer_set_packet(tx_buffer, sizeof(zero_copy_payload), SPW_TERMINATOR_EOP) != SPW_OK ||
        spw_port_submit_tx_buffer(port, &tx_buffer, 50000u) != SPW_OK ||
        tx_buffer != NULL) {
        return 0x302;
    }

    g_stm32h755_spwkit_evidence.phase = 4u;
    if (spw_port_acquire_rx_buffer(port, 50000u, &rx_buffer) != SPW_OK ||
        rx_buffer == NULL ||
        spw_buffer_get_view(rx_buffer, &rx_view) != SPW_OK ||
        rx_view.data != g_dma_rx ||
        rx_view.length != sizeof(zero_copy_payload) ||
        rx_view.terminator != SPW_TERMINATOR_EOP ||
        !bytes_equal(rx_view.data, zero_copy_payload, sizeof(zero_copy_payload)) ||
        spw_port_release_rx_buffer(port, &rx_buffer) != SPW_OK || rx_buffer != NULL) {
        return 0x401;
    }

    g_stm32h755_spwkit_evidence.phase = 5u;
    if (spw_port_reclaim_tx_buffer(port, 50000u, &reclaimed) != SPW_OK ||
        reclaimed == NULL ||
        spw_buffer_get_view(reclaimed, &reclaimed_view) != SPW_OK ||
        reclaimed_view.data != submitted_pointer ||
        spw_port_release_tx_buffer(port, &reclaimed) != SPW_OK || reclaimed != NULL) {
        return 0x501;
    }

    g_stm32h755_spwkit_evidence.phase = 6u;
    if (g_driver.sync_to_device == 0u || g_driver.sync_from_device == 0u ||
        g_driver.dma_transfers < 2u ||
        spw_port_get_statistics(port, &statistics) != SPW_OK ||
        statistics.tx_packets < 2u || statistics.rx_packets < 2u) {
        return 0x601;
    }

    if (spw_port_close(port) != SPW_OK) {
        return 0x602;
    }

    g_stm32h755_spwkit_evidence.sync_to_device = g_driver.sync_to_device;
    g_stm32h755_spwkit_evidence.sync_from_device = g_driver.sync_from_device;
    g_stm32h755_spwkit_evidence.dma_transfers = g_driver.dma_transfers;
    g_stm32h755_spwkit_evidence.tx_packets = (uint32_t)statistics.tx_packets;
    g_stm32h755_spwkit_evidence.rx_packets = (uint32_t)statistics.rx_packets;
    return 0;
}

void Reset_Handler(void) {
    int result;

    __disable_irq();
    memory_init();
    SCB->VTOR = (uint32_t)(uintptr_t)g_stm32h755_vector_table;

    /* Enable DMA2 on the D2 AHB1 bus. The reset clock configuration is enough
     * for this standalone memory-to-memory evidence firmware. */
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA2EN;
    (void)RCC->AHB1ENR;

    SCB_EnableICache();
    SCB_EnableDCache();

    g_driver.link_state = SPW_LINK_ERROR_RESET;
    g_driver.tx_owner = STM32_DMA_FREE;
    g_driver.rx_owner = STM32_DMA_FREE;
    g_stm32h755_spwkit_evidence.magic = STM32_EVIDENCE_MAGIC;
    g_stm32h755_spwkit_evidence.result = UINT32_C(0xfffffffe);

    result = run_contract();
    g_stm32h755_spwkit_evidence.result = (uint32_t)result;
    g_stm32h755_spwkit_evidence.phase = result == 0 ? 0x600du : 0xdead0000u | (uint32_t)result;
    __DSB();

    for (;;) {
        __NOP();
    }
}
