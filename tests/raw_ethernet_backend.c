// SPDX-License-Identifier: Apache-2.0
#include <spwkit/spwkit.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    TEST_FRAME_CAPACITY = 1514u,
    TEST_QUEUE_DEPTH = 16u
};

typedef struct test_frame {
    uint8_t bytes[TEST_FRAME_CAPACITY];
    size_t size;
} test_frame_t;

typedef struct test_queue {
    test_frame_t frames[TEST_QUEUE_DEPTH];
    size_t head;
    size_t count;
} test_queue_t;

typedef struct test_link {
    test_queue_t inbox[2];
} test_link_t;

typedef struct test_endpoint {
    test_link_t* link;
    size_t index;
    bool started;
    bool link_up;
    uint8_t last_tx[TEST_FRAME_CAPACITY];
    size_t last_tx_size;
} test_endpoint_t;

typedef struct test_clock {
    uint64_t now_us;
} test_clock_t;

static size_t other_index(const test_endpoint_t* endpoint) {
    return endpoint->index == 0u ? 1u : 0u;
}

static spw_result_t io_start(void* context) {
    test_endpoint_t* endpoint = (test_endpoint_t*)context;
    endpoint->started = true;
    return SPW_OK;
}

static spw_result_t io_stop(void* context) {
    test_endpoint_t* endpoint = (test_endpoint_t*)context;
    endpoint->started = false;
    return SPW_OK;
}

static spw_result_t io_reset(void* context) {
    test_endpoint_t* endpoint = (test_endpoint_t*)context;
    endpoint->started = false;
    endpoint->link->inbox[endpoint->index].head = 0u;
    endpoint->link->inbox[endpoint->index].count = 0u;
    return SPW_OK;
}

static spw_result_t io_send_frame(void* context,
                                  const uint8_t* frame,
                                  size_t frame_size,
                                  spw_timeout_us_t timeout_us) {
    test_endpoint_t* endpoint = (test_endpoint_t*)context;
    test_queue_t* queue;
    test_frame_t* slot;
    size_t slot_index;
    (void)timeout_us;

    if (!endpoint->started) {
        return SPW_ERR_INVALID_STATE;
    }
    if (frame == NULL || frame_size > TEST_FRAME_CAPACITY) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    queue = &endpoint->link->inbox[other_index(endpoint)];
    if (queue->count == TEST_QUEUE_DEPTH) {
        return SPW_ERR_RESOURCE_EXHAUSTED;
    }

    slot_index = (queue->head + queue->count) % TEST_QUEUE_DEPTH;
    slot = &queue->frames[slot_index];
    memcpy(slot->bytes, frame, frame_size);
    slot->size = frame_size;
    ++queue->count;

    memcpy(endpoint->last_tx, frame, frame_size);
    endpoint->last_tx_size = frame_size;
    return SPW_OK;
}

static spw_result_t io_receive_frame(void* context,
                                     uint8_t* frame,
                                     size_t frame_capacity,
                                     size_t* out_frame_size,
                                     spw_timeout_us_t timeout_us) {
    test_endpoint_t* endpoint = (test_endpoint_t*)context;
    test_queue_t* queue;
    test_frame_t* slot;
    (void)timeout_us;

    if (out_frame_size == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_frame_size = 0u;
    if (!endpoint->started) {
        return SPW_ERR_INVALID_STATE;
    }
    queue = &endpoint->link->inbox[endpoint->index];
    if (queue->count == 0u) {
        return SPW_ERR_TIMEOUT;
    }
    slot = &queue->frames[queue->head];
    *out_frame_size = slot->size;
    if (frame_capacity < slot->size || frame == NULL) {
        return SPW_ERR_BUFFER_TOO_SMALL;
    }
    memcpy(frame, slot->bytes, slot->size);
    queue->head = (queue->head + 1u) % TEST_QUEUE_DEPTH;
    --queue->count;
    return SPW_OK;
}

static spw_result_t io_wait(void* context,
                            spw_raw_ethernet_ready_t interests,
                            spw_timeout_us_t timeout_us,
                            spw_raw_ethernet_ready_t* out_ready) {
    test_endpoint_t* endpoint = (test_endpoint_t*)context;
    spw_raw_ethernet_ready_t ready = SPW_RAW_ETHERNET_READY_NONE;
    (void)timeout_us;

    if (out_ready == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    if (!endpoint->started) {
        *out_ready = SPW_RAW_ETHERNET_READY_NONE;
        return SPW_ERR_INVALID_STATE;
    }
    if ((interests & SPW_RAW_ETHERNET_READY_RX) != 0u &&
        endpoint->link->inbox[endpoint->index].count != 0u) {
        ready |= SPW_RAW_ETHERNET_READY_RX;
    }
    if ((interests & SPW_RAW_ETHERNET_READY_TX) != 0u &&
        endpoint->link->inbox[other_index(endpoint)].count < TEST_QUEUE_DEPTH) {
        ready |= SPW_RAW_ETHERNET_READY_TX;
    }
    *out_ready = ready;
    return ready == SPW_RAW_ETHERNET_READY_NONE ? SPW_ERR_TIMEOUT : SPW_OK;
}

static spw_result_t io_get_max_frame_size(const void* context,
                                          size_t* out_frame_size) {
    (void)context;
    if (out_frame_size == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_frame_size = TEST_FRAME_CAPACITY;
    return SPW_OK;
}

static spw_result_t io_get_link_up(const void* context,
                                   bool* out_link_up) {
    const test_endpoint_t* endpoint = (const test_endpoint_t*)context;
    if (out_link_up == NULL) {
        return SPW_ERR_INVALID_ARGUMENT;
    }
    *out_link_up = endpoint->link_up;
    return SPW_OK;
}

static uint64_t runtime_now_us(const void* context) {
    return ((const test_clock_t*)context)->now_us;
}

static spw_result_t runtime_delay_us(void* context,
                                     uint64_t delay_us,
                                     spw_timeout_us_t timeout_us) {
    test_clock_t* clock = (test_clock_t*)context;
    if (timeout_us != SPW_TIMEOUT_INFINITE && timeout_us < delay_us) {
        return SPW_ERR_TIMEOUT;
    }
    clock->now_us += delay_us;
    return SPW_OK;
}

static const spw_raw_ethernet_io_ops_t IO_OPS = {
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

static const spw_runtime_ops_t RUNTIME_OPS = {
    sizeof(spw_runtime_ops_t),
    SPW_RUNTIME_OPS_VERSION,
    runtime_now_us,
    runtime_delay_us
};

static void* aligned_workspace(size_t size,
                               size_t alignment,
                               void** out_allocation) {
    uintptr_t aligned;
    uint8_t* allocation =
        (uint8_t*)malloc(size + alignment - 1u);
    assert(allocation != NULL);
    aligned = ((uintptr_t)allocation + alignment - 1u) &
              ~((uintptr_t)alignment - 1u);
    *out_allocation = allocation;
    return (void*)aligned;
}

static void configure(spw_raw_ethernet_config_t* raw,
                      test_endpoint_t* endpoint,
                      test_clock_t* clock,
                      const uint8_t local_mac[SPW_RAW_ETHERNET_MAC_SIZE],
                      const uint8_t remote_mac[SPW_RAW_ETHERNET_MAC_SIZE]) {
    *raw = (spw_raw_ethernet_config_t)
        SPW_RAW_ETHERNET_CONFIG_INITIALIZER(
            &IO_OPS, endpoint, &RUNTIME_OPS, clock, 9u);
    memcpy(raw->local_mac, local_mac, SPW_RAW_ETHERNET_MAC_SIZE);
    memcpy(raw->remote_mac, remote_mac, SPW_RAW_ETHERNET_MAC_SIZE);
    raw->ether_type = SPW_RAW_ETHERNET_LOCAL_EXPERIMENTAL_ETHERTYPE;
}

int main(void) {
    static test_link_t link;
    test_endpoint_t endpoint_a;
    test_endpoint_t endpoint_b;
    test_clock_t clock_a = {100u};
    test_clock_t clock_b = {200u};
    const uint8_t mac_a[SPW_RAW_ETHERNET_MAC_SIZE] =
        {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0xa1u};
    const uint8_t mac_b[SPW_RAW_ETHERNET_MAC_SIZE] =
        {0x02u, 0x00u, 0x00u, 0x00u, 0x00u, 0xb2u};
    spw_raw_ethernet_config_t raw_a;
    spw_raw_ethernet_config_t raw_b;
    spw_port_config_t port_config_a =
        SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_RAW_ETHERNET);
    spw_port_config_t port_config_b =
        SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_RAW_ETHERNET);
    spw_port_workspace_requirements_t req_a;
    spw_port_workspace_requirements_t req_b;
    void* allocation_a = NULL;
    void* allocation_b = NULL;
    void* workspace_a;
    void* workspace_b;
    spw_port_t* port_a = NULL;
    spw_port_t* port_b = NULL;
    spw_link_state_t state_a = SPW_LINK_ERROR_RESET;
    spw_link_state_t state_b = SPW_LINK_ERROR_RESET;
    uint8_t tx[1500];
    uint8_t rx[1500];
    spw_packet_t tx_packet;
    spw_packet_t rx_packet;
    spw_time_code_t tx_time = {23u, 0u};
    spw_time_code_t rx_time = {0u, 0u};
    size_t i;

    memset(&link, 0, sizeof(link));
    memset(&endpoint_a, 0, sizeof(endpoint_a));
    memset(&endpoint_b, 0, sizeof(endpoint_b));
    endpoint_a.link = &link;
    endpoint_a.index = 0u;
    endpoint_a.link_up = true;
    endpoint_b.link = &link;
    endpoint_b.index = 1u;
    endpoint_b.link_up = true;

    configure(&raw_a, &endpoint_a, &clock_a, mac_a, mac_b);
    configure(&raw_b, &endpoint_b, &clock_b, mac_b, mac_a);

    port_config_a.backend_config = &raw_a;
    port_config_a.backend_config_size = sizeof(raw_a);
    port_config_b.backend_config = &raw_b;
    port_config_b.backend_config_size = sizeof(raw_b);

    assert(spw_port_workspace_requirements(&port_config_a, &req_a) == SPW_OK);
    assert(spw_port_workspace_requirements(&port_config_b, &req_b) == SPW_OK);
    workspace_a = aligned_workspace(req_a.size, req_a.alignment, &allocation_a);
    workspace_b = aligned_workspace(req_b.size, req_b.alignment, &allocation_b);

    assert(spw_port_open_in_place(
               &port_config_a, workspace_a, req_a.size, &port_a) == SPW_OK);
    assert(spw_port_open_in_place(
               &port_config_b, workspace_b, req_b.size, &port_b) == SPW_OK);
    assert(spw_port_start(port_a) == SPW_OK);
    assert(spw_port_start(port_b) == SPW_OK);

    assert(spw_port_get_link_state(port_a, &state_a) == SPW_OK);
    assert(spw_port_get_link_state(port_b, &state_b) == SPW_OK);
    assert(state_a == SPW_LINK_RUN);
    assert(state_b == SPW_LINK_RUN);

    for (i = 0u; i < sizeof(tx); ++i) {
        tx[i] = (uint8_t)(i & 0xffu);
    }
    memset(rx, 0, sizeof(rx));

    memset(&tx_packet, 0, sizeof(tx_packet));
    tx_packet.data = tx;
    tx_packet.length = sizeof(tx);
    tx_packet.capacity = sizeof(tx);
    tx_packet.terminator = SPW_TERMINATOR_EEP;
    memset(&rx_packet, 0, sizeof(rx_packet));
    rx_packet.data = rx;
    rx_packet.capacity = sizeof(rx);

    assert(spw_port_send(port_a, &tx_packet, 1000u) == SPW_OK);
    assert(endpoint_a.last_tx_size >= 20u);
    assert(endpoint_a.last_tx[12] == 0x88u);
    assert(endpoint_a.last_tx[13] == 0xb5u);
    assert(endpoint_a.last_tx[14] ==
           (uint8_t)(SPW_RAW_ETHERNET_PROTOCOL_SUBTYPE >> 8u));
    assert(endpoint_a.last_tx[15] ==
           (uint8_t)SPW_RAW_ETHERNET_PROTOCOL_SUBTYPE);
    assert(endpoint_a.last_tx[16] ==
           SPW_RAW_ETHERNET_PROTOCOL_VERSION_MAJOR);
    assert(endpoint_a.last_tx[17] ==
           SPW_RAW_ETHERNET_PROTOCOL_VERSION_MINOR);
    {
        const size_t declared_vspw_size =
            ((size_t)endpoint_a.last_tx[18] << 8u) |
            (size_t)endpoint_a.last_tx[19];
        assert(endpoint_a.last_tx_size == 20u + declared_vspw_size);
    }

    assert(spw_port_receive(port_b, &rx_packet, 1000u) == SPW_OK);
    assert(rx_packet.length == sizeof(tx));
    assert(rx_packet.terminator == SPW_TERMINATOR_EEP);
    assert(memcmp(tx, rx, sizeof(tx)) == 0);

    /* Service the ACK produced by B before B starts its own reliable TX. */
    assert(spw_port_get_link_state(port_a, &state_a) == SPW_OK);
    assert(state_a == SPW_LINK_RUN);

    assert(spw_port_send_time_code(port_b, &tx_time, 1000u) == SPW_OK);
    assert(spw_port_receive_time_code(port_a, &rx_time, 1000u) == SPW_OK);
    assert(rx_time.time_count == tx_time.time_count);
    assert(rx_time.control_flags == tx_time.control_flags);

    assert(spw_port_stop(port_a) == SPW_OK);
    assert(spw_port_stop(port_b) == SPW_OK);
    assert(spw_port_close(port_a) == SPW_OK);
    assert(spw_port_close(port_b) == SPW_OK);

    free(allocation_a);
    free(allocation_b);
    return 0;
}
