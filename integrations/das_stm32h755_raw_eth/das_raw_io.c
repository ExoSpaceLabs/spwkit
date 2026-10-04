// SPDX-License-Identifier: Apache-2.0
#include "das_raw_io.h"

#include <das/time.h>

#include <string.h>

#define SPW_DAS_DEMCR (*(volatile uint32_t*)UINT32_C(0xE000EDFC))
#define SPW_DAS_DWT_CTRL (*(volatile uint32_t*)UINT32_C(0xE0001000))
#define SPW_DAS_DWT_CYCCNT (*(volatile uint32_t*)UINT32_C(0xE0001004))
#define SPW_DAS_DEMCR_TRCENA (UINT32_C(1) << 24u)
#define SPW_DAS_DWT_CTRL_CYCCNTENA (UINT32_C(1) << 0u)

static void cycle_counter_prepare(void) {
    SPW_DAS_DEMCR |= SPW_DAS_DEMCR_TRCENA;
    SPW_DAS_DWT_CYCCNT = 0u;
    SPW_DAS_DWT_CTRL |= SPW_DAS_DWT_CTRL_CYCCNTENA;
}

uint32_t spw_das_cycle_counter_read(void) {
    __asm__ __volatile__("" ::: "memory");
    return SPW_DAS_DWT_CYCCNT;
}

static void record_cycles(spw_das_cycle_stats_t* stats, uint32_t cycles) {
    if (stats->count == 0u || cycles < stats->min_cycles) {
        stats->min_cycles = cycles;
    }
    if (cycles > stats->max_cycles) {
        stats->max_cycles = cycles;
    }
    ++stats->count;
    stats->total_cycles += (uint64_t)cycles;
}

static uint32_t elapsed_cycles(uint32_t start) {
    return spw_das_cycle_counter_read() - start;
}

static spw_result_t map_result(das_result_t result) {
    switch (result) {
    case DAS_OK: return SPW_OK;
    case DAS_ERROR_INVALID_ARGUMENT: return SPW_ERR_INVALID_ARGUMENT;
    case DAS_ERROR_UNSUPPORTED: return SPW_ERR_UNSUPPORTED;
    case DAS_ERROR_TIMEOUT: return SPW_ERR_TIMEOUT;
    case DAS_ERROR_NOT_READY: return SPW_ERR_LINK_UNAVAILABLE;
    case DAS_ERROR_IO:
    default:
        return SPW_ERR_BACKEND;
    }
}

static bool timeout_expired(das_time_ms_t start,
                            spw_timeout_us_t timeout_us) {
    if (timeout_us == SPW_TIMEOUT_INFINITE) {
        return false;
    }
    return (uint64_t)das_time_elapsed_ms(start) * UINT64_C(1000) >= timeout_us;
}

static spw_result_t poll_one(spw_das_raw_io_t* context) {
    size_t received = 0u;
    das_result_t result;

    if (context->pending_valid) {
        return SPW_OK;
    }
    {
        const uint32_t start = spw_das_cycle_counter_read();
        result = das_eth_receive(context->eth,
                                 context->pending_frame,
                                 sizeof(context->pending_frame),
                                 &received);
        const uint32_t cycles = elapsed_cycles(start);
        record_cycles(&context->stats.rx_poll, cycles);
        if (result != DAS_OK) {
            ++context->stats.rx_errors;
            return map_result(result);
        }
        if (received != 0u) {
            record_cycles(&context->stats.rx_success, cycles);
            context->stats.last_rx_size = (uint32_t)received;
            if (received >= sizeof(context->stats.last_rx_header)) {
                memcpy(context->stats.last_rx_header,
                       context->pending_frame,
                       sizeof(context->stats.last_rx_header));
            }
            context->pending_size = received;
            context->pending_valid = true;
        } else {
            ++context->stats.rx_empty_polls;
        }
    }
    return SPW_OK;
}

static spw_result_t wait_for_rx(spw_das_raw_io_t* context,
                                spw_timeout_us_t timeout_us) {
    const das_time_ms_t start = das_time_now_ms();

    for (;;) {
        spw_result_t result = poll_one(context);
        if (result != SPW_OK) {
            return result;
        }
        if (context->pending_valid) {
            return SPW_OK;
        }
        if (timeout_us == SPW_TIMEOUT_IMMEDIATE ||
            timeout_expired(start, timeout_us)) {
            return SPW_ERR_TIMEOUT;
        }
        (void)das_delay_ms(1u);
    }
}

static spw_result_t io_start(void* raw) {
    spw_das_raw_io_t* context = (spw_das_raw_io_t*)raw;
    if (context == NULL || !das_eth_is_valid(context->eth)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    context->started = true;
    return SPW_OK;
}

static spw_result_t io_stop(void* raw) {
    spw_das_raw_io_t* context = (spw_das_raw_io_t*)raw;
    if (context == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    context->started = false;
    context->pending_valid = false;
    context->pending_size = 0u;
    return SPW_OK;
}

static spw_result_t io_reset(void* raw) {
    return io_stop(raw);
}

static spw_result_t io_send_frame(void* raw,
                                  const uint8_t* frame,
                                  size_t frame_size,
                                  spw_timeout_us_t timeout_us) {
    spw_das_raw_io_t* context = (spw_das_raw_io_t*)raw;
    das_eth_link_state_t link = {0};
    das_result_t result;
    (void)timeout_us;

    if (context == NULL || frame == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!context->started) {
        return SPW_ERR_INVALID_STATE;
    }
    result = das_eth_link_state(context->eth, &link);
    if (result != DAS_OK) {
        return map_result(result);
    }
    if (!link.up) {
        return SPW_ERR_LINK_UNAVAILABLE;
    }
    {
        const uint32_t start = spw_das_cycle_counter_read();
        const das_result_t send_result =
            das_eth_send(context->eth, frame, frame_size);
        record_cycles(&context->stats.tx_send, elapsed_cycles(start));
        context->stats.last_tx_size = (uint32_t)frame_size;
        if (frame_size >= sizeof(context->stats.last_tx_header)) {
            memcpy(context->stats.last_tx_header,
                   frame,
                   sizeof(context->stats.last_tx_header));
        }
        if (send_result == DAS_OK) {
            ++context->stats.tx_successes;
        } else {
            ++context->stats.tx_failures;
        }
        return map_result(send_result);
    }
}

static spw_result_t io_receive_frame(void* raw,
                                     uint8_t* frame,
                                     size_t frame_capacity,
                                     size_t* out_frame_size,
                                     spw_timeout_us_t timeout_us) {
    spw_das_raw_io_t* context = (spw_das_raw_io_t*)raw;
    spw_result_t result;

    if (context == NULL || !context->started || frame == NULL ||
        out_frame_size == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_frame_size = 0u;
    result = wait_for_rx(context, timeout_us);
    if (result != SPW_OK) {
        return result;
    }
    if (context->pending_size > frame_capacity) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }

    memcpy(frame, context->pending_frame, context->pending_size);
    *out_frame_size = context->pending_size;
    context->pending_valid = false;
    context->pending_size = 0u;
    return SPW_OK;
}

static spw_result_t io_wait(void* raw,
                            spw_raw_ethernet_ready_t interests,
                            spw_timeout_us_t timeout_us,
                            spw_raw_ethernet_ready_t* out_ready) {
    spw_das_raw_io_t* context = (spw_das_raw_io_t*)raw;
    das_eth_link_state_t link = {0};
    spw_result_t rx_result = SPW_ERR_TIMEOUT;
    das_result_t link_result;

    if (context == NULL || !context->started || out_ready == NULL ||
        interests == SPW_RAW_ETHERNET_READY_NONE) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_ready = SPW_RAW_ETHERNET_READY_NONE;

    /* TX readiness is level-triggered and should not wait behind an RX poll. */
    if ((interests & SPW_RAW_ETHERNET_READY_TX) != 0u) {
        link_result = das_eth_link_state(context->eth, &link);
        if (link_result != DAS_OK) {
            return map_result(link_result);
        }
        if (link.up) {
            *out_ready |= SPW_RAW_ETHERNET_READY_TX;
        }
    }

    if ((interests & SPW_RAW_ETHERNET_READY_RX) != 0u) {
        rx_result = wait_for_rx(
            context,
            *out_ready != SPW_RAW_ETHERNET_READY_NONE
                ? SPW_TIMEOUT_IMMEDIATE
                : timeout_us);
        if (rx_result == SPW_OK) {
            *out_ready |= SPW_RAW_ETHERNET_READY_RX;
        } else if (rx_result != SPW_ERR_TIMEOUT) {
            return rx_result;
        }
    }

    return *out_ready == SPW_RAW_ETHERNET_READY_NONE
               ? SPW_ERR_TIMEOUT
               : SPW_OK;
}

static spw_result_t io_get_max_frame_size(const void* raw,
                                          size_t* out_frame_size) {
    const spw_das_raw_io_t* context = (const spw_das_raw_io_t*)raw;
    if (context == NULL || out_frame_size == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_frame_size = DAS_ETH_MAX_FRAME_SIZE;
    return SPW_OK;
}

static spw_result_t io_get_link_up(const void* raw, bool* out_link_up) {
    const spw_das_raw_io_t* context = (const spw_das_raw_io_t*)raw;
    das_eth_link_state_t link = {0};
    das_result_t result;

    if (context == NULL || out_link_up == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    result = das_eth_link_state(context->eth, &link);
    if (result != DAS_OK) {
        return map_result(result);
    }
    *out_link_up = link.up;
    return SPW_OK;
}

static uint64_t runtime_now_us(const void* context) {
    (void)context;
    return (uint64_t)das_time_now_ms() * UINT64_C(1000);
}

static spw_result_t runtime_delay_us(void* context,
                                     uint64_t delay_us,
                                     spw_timeout_us_t timeout_us) {
    uint64_t rounded_ms;
    (void)context;

    if (delay_us == 0u) {
        return SPW_OK;
    }
    if (timeout_us != SPW_TIMEOUT_INFINITE && timeout_us < delay_us) {
        return SPW_ERR_TIMEOUT;
    }
    rounded_ms = (delay_us + UINT64_C(999)) / UINT64_C(1000);
    if (rounded_ms > UINT32_MAX) {
        return SPW_ERR_TIMEOUT;
    }
    return map_result(das_delay_ms((uint32_t)rounded_ms));
}

void spw_das_raw_io_init(spw_das_raw_io_t* context, das_eth_t eth) {
    if (context == NULL) {
        return;
    }
    memset(context, 0, sizeof(*context));
    context->eth = eth;
    cycle_counter_prepare();
}

void spw_das_raw_io_get_stats(const spw_das_raw_io_t* context,
                              spw_das_raw_io_stats_t* out_stats) {
    if (context == NULL || out_stats == NULL) {
        return;
    }
    *out_stats = context->stats;
}

const spw_raw_ethernet_io_ops_t SPW_DAS_RAW_ETHERNET_IO_OPS = {
    sizeof(spw_raw_ethernet_io_ops_t),
    SPW_RAW_ETHERNET_IO_OPS_VERSION,
    io_start,
    io_stop,
    io_reset,
    io_send_frame,
    io_receive_frame,
    io_wait,
    io_get_max_frame_size,
    io_get_link_up
};

const spw_runtime_ops_t SPW_DAS_RUNTIME_OPS = {
    sizeof(spw_runtime_ops_t),
    SPW_RUNTIME_OPS_VERSION,
    runtime_now_us,
    runtime_delay_us
};
