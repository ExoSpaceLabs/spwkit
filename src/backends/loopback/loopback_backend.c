// SPDX-License-Identifier: Apache-2.0

#include "backends/loopback/loopback_backend.h"

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
    SPW_LOOPBACK_MAX_PACKET_SIZE = 4096,
    SPW_LOOPBACK_PACKET_QUEUE_DEPTH = 8,
    SPW_LOOPBACK_TIME_CODE_QUEUE_DEPTH = 8
};

/*
 * MSVC's C library does not expose max_align_t consistently in C mode.
 * This private union gives the loopback capability a portable alignment for
 * the fundamental scalar/pointer types without introducing a C++ dependency.
 */
typedef union spw_loopback_max_alignment {
    long double long_double_value;
    void* pointer_value;
    uint64_t integer_value;
} spw_loopback_max_alignment_t;

typedef struct spw_loopback_packet_slot {
    uint8_t data[SPW_LOOPBACK_MAX_PACKET_SIZE];
    size_t length;
    spw_terminator_t terminator;
} spw_loopback_packet_slot_t;

typedef struct spw_loopback_packet_queue {
    spw_loopback_packet_slot_t slots[SPW_LOOPBACK_PACKET_QUEUE_DEPTH];
    size_t head;
    size_t tail;
    size_t count;
} spw_loopback_packet_queue_t;

typedef struct spw_loopback_time_code_queue {
    spw_time_code_t slots[SPW_LOOPBACK_TIME_CODE_QUEUE_DEPTH];
    size_t head;
    size_t tail;
    size_t count;
} spw_loopback_time_code_queue_t;

typedef struct spw_loopback_backend {
    spw_link_state_t state;
    spw_loopback_packet_queue_t packets;
    spw_loopback_time_code_queue_t time_codes;
    spw_statistics_t statistics;
} spw_loopback_backend_t;

static bool valid_terminator(spw_terminator_t terminator) {
    return terminator == SPW_TERMINATOR_EOP || terminator == SPW_TERMINATOR_EEP;
}

static bool valid_time_code(const spw_time_code_t* time_code) {
    return time_code != NULL && time_code->time_count <= 63u &&
           time_code->control_flags == 0u;
}

static void clear_queues(spw_loopback_backend_t* backend) {
    memset(&backend->packets, 0, sizeof(backend->packets));
    memset(&backend->time_codes, 0, sizeof(backend->time_codes));
}

static spw_result_t loopback_construct(void* context,
                                       const spw_port_config_t* config) {
    spw_loopback_backend_t* backend = (spw_loopback_backend_t*)context;
    (void)config;
    memset(backend, 0, sizeof(*backend));
    backend->state = SPW_LINK_ERROR_RESET;
    return SPW_OK;
}

static void loopback_destroy(void* context) {
    (void)context;
}

static spw_result_t loopback_start(void* context) {
    spw_loopback_backend_t* backend = (spw_loopback_backend_t*)context;
    backend->state = SPW_LINK_RUN;
    return SPW_OK;
}

static spw_result_t loopback_stop(void* context) {
    spw_loopback_backend_t* backend = (spw_loopback_backend_t*)context;
    backend->state = SPW_LINK_READY;
    return SPW_OK;
}

static spw_result_t loopback_reset(void* context) {
    spw_loopback_backend_t* backend = (spw_loopback_backend_t*)context;
    clear_queues(backend);
    backend->state = SPW_LINK_ERROR_RESET;
    return SPW_OK;
}

static spw_result_t loopback_get_link_state(const void* context,
                                             spw_link_state_t* out_state) {
    const spw_loopback_backend_t* backend =
        (const spw_loopback_backend_t*)context;
    *out_state = backend->state;
    return SPW_OK;
}

static spw_result_t loopback_get_capabilities(
    const void* context,
    spw_capabilities_t* out_capabilities) {
    (void)context;
    out_capabilities->bits = SPW_CAP_EEP | SPW_CAP_TIME_CODE |
                             SPW_CAP_LINK_CONTROL | SPW_CAP_STATISTICS;
    out_capabilities->max_packet_size = SPW_LOOPBACK_MAX_PACKET_SIZE;
    out_capabilities->tx_queue_depth = SPW_LOOPBACK_PACKET_QUEUE_DEPTH;
    out_capabilities->rx_queue_depth = SPW_LOOPBACK_PACKET_QUEUE_DEPTH;
    out_capabilities->buffer_alignment = alignof(spw_loopback_max_alignment_t);
    return SPW_OK;
}

static bool loopback_supports_zero_copy(const void* context) {
    (void)context;
    return false;
}

static spw_result_t loopback_send(void* context,
                                  const spw_packet_t* packet,
                                  spw_timeout_us_t timeout_us) {
    spw_loopback_backend_t* backend = (spw_loopback_backend_t*)context;
    spw_loopback_packet_slot_t* slot;
    (void)timeout_us;

    if (backend->state != SPW_LINK_RUN) {
        return SPW_ERR_INVALID_STATE;
    }
    if (!valid_terminator(packet->terminator) ||
        packet->length > SPW_LOOPBACK_MAX_PACKET_SIZE) {
        return SPW_ERR_INVALID_PACKET;
    }
    if (packet->length > 0u && packet->data == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (packet->capacity != 0u && packet->capacity < packet->length) {
        return SPW_ERR_INVALID_PACKET;
    }
    if (backend->packets.count == SPW_LOOPBACK_PACKET_QUEUE_DEPTH) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    slot = &backend->packets.slots[backend->packets.tail];
    if (packet->length > 0u) {
        memcpy(slot->data, packet->data, packet->length);
    }
    slot->length = packet->length;
    slot->terminator = packet->terminator;

    backend->packets.tail =
        (backend->packets.tail + 1u) % SPW_LOOPBACK_PACKET_QUEUE_DEPTH;
    ++backend->packets.count;

    ++backend->statistics.tx_packets;
    backend->statistics.tx_bytes += packet->length;
    if (packet->terminator == SPW_TERMINATOR_EEP) {
        ++backend->statistics.eep_packets;
    }
    return SPW_OK;
}

static spw_result_t loopback_receive(void* context,
                                     spw_packet_t* packet,
                                     spw_timeout_us_t timeout_us) {
    spw_loopback_backend_t* backend = (spw_loopback_backend_t*)context;
    const spw_loopback_packet_slot_t* slot;
    (void)timeout_us;

    if (backend->state != SPW_LINK_RUN) {
        return SPW_ERR_INVALID_STATE;
    }
    if (backend->packets.count == 0u) {
        return SPW_ERR_TIMEOUT;
    }

    slot = &backend->packets.slots[backend->packets.head];
    packet->length = slot->length;
    packet->terminator = slot->terminator;

    if (slot->length > packet->capacity) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }
    if (slot->length > 0u && packet->data == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (slot->length > 0u) {
        memcpy(packet->data, slot->data, slot->length);
    }

    backend->packets.head =
        (backend->packets.head + 1u) % SPW_LOOPBACK_PACKET_QUEUE_DEPTH;
    --backend->packets.count;

    ++backend->statistics.rx_packets;
    backend->statistics.rx_bytes += slot->length;
    return SPW_OK;
}

static spw_result_t loopback_send_time_code(void* context,
                                            const spw_time_code_t* time_code,
                                            spw_timeout_us_t timeout_us) {
    spw_loopback_backend_t* backend = (spw_loopback_backend_t*)context;
    (void)timeout_us;

    if (backend->state != SPW_LINK_RUN) {
        return SPW_ERR_INVALID_STATE;
    }
    if (!valid_time_code(time_code)) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (backend->time_codes.count == SPW_LOOPBACK_TIME_CODE_QUEUE_DEPTH) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    backend->time_codes.slots[backend->time_codes.tail] = *time_code;
    backend->time_codes.tail =
        (backend->time_codes.tail + 1u) % SPW_LOOPBACK_TIME_CODE_QUEUE_DEPTH;
    ++backend->time_codes.count;
    ++backend->statistics.tx_time_codes;
    return SPW_OK;
}

static spw_result_t loopback_receive_time_code(void* context,
                                               spw_time_code_t* time_code,
                                               spw_timeout_us_t timeout_us) {
    spw_loopback_backend_t* backend = (spw_loopback_backend_t*)context;
    (void)timeout_us;

    if (backend->state != SPW_LINK_RUN) {
        return SPW_ERR_INVALID_STATE;
    }
    if (backend->time_codes.count == 0u) {
        return SPW_ERR_TIMEOUT;
    }

    *time_code = backend->time_codes.slots[backend->time_codes.head];
    backend->time_codes.head =
        (backend->time_codes.head + 1u) % SPW_LOOPBACK_TIME_CODE_QUEUE_DEPTH;
    --backend->time_codes.count;
    ++backend->statistics.rx_time_codes;
    return SPW_OK;
}

static spw_result_t loopback_get_statistics(
    const void* context,
    spw_statistics_t* out_statistics) {
    const spw_loopback_backend_t* backend =
        (const spw_loopback_backend_t*)context;
    *out_statistics = backend->statistics;
    return SPW_OK;
}

static spw_result_t loopback_clear_statistics(void* context) {
    spw_loopback_backend_t* backend = (spw_loopback_backend_t*)context;
    memset(&backend->statistics, 0, sizeof(backend->statistics));
    return SPW_OK;
}

static const spw_backend_ops_t LOOPBACK_OPS = {
    loopback_start,
    loopback_stop,
    loopback_reset,
    loopback_get_link_state,
    loopback_get_capabilities,
    loopback_supports_zero_copy,
    loopback_send,
    loopback_receive,
    loopback_send_time_code,
    loopback_receive_time_code,
    loopback_get_statistics,
    loopback_clear_statistics,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

static const spw_backend_factory_t LOOPBACK_FACTORY = {
    sizeof(spw_loopback_backend_t),
    alignof(spw_loopback_backend_t),
    loopback_construct,
    loopback_destroy,
    &LOOPBACK_OPS,
    NULL
};

const spw_backend_factory_t* spw_loopback_backend_factory(void) {
    return &LOOPBACK_FACTORY;
}
