// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/in_memory_transport.h"
#include "backends/ethernet/transport_provider.h"
#include "backends/ethernet/vspw_runtime.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct fake_runtime {
    uint64_t now_us;
    uint64_t delayed_us;
} fake_runtime_t;

static uint64_t fake_now_us(void* context) {
    return ((fake_runtime_t*)context)->now_us;
}

static spw_result_t fake_delay_us(void* context,
                                  uint64_t delay_us,
                                  spw_timeout_us_t timeout_us) {
    fake_runtime_t* runtime = (fake_runtime_t*)context;
    if (timeout_us != SPW_TIMEOUT_INFINITE && timeout_us < delay_us) {
        return SPW_ERR_TIMEOUT;
    }
    runtime->now_us += delay_us;
    runtime->delayed_us += delay_us;
    return SPW_OK;
}

static void make_peer(spw_transport_peer_id_t* peer, uint8_t value) {
    assert(spw_transport_peer_id_set(peer, &value, sizeof(value)));
}

static void test_peer_identity(void) {
    spw_transport_peer_id_t a = SPW_TRANSPORT_PEER_ID_INITIALIZER;
    spw_transport_peer_id_t b = SPW_TRANSPORT_PEER_ID_INITIALIZER;
    uint8_t bytes[SPW_TRANSPORT_PEER_ID_MAX_SIZE + 1u] = {0};

    assert(spw_transport_peer_id_equal(&a, &b));
    make_peer(&a, 0x11u);
    make_peer(&b, 0x11u);
    assert(spw_transport_peer_id_equal(&a, &b));
    make_peer(&b, 0x22u);
    assert(!spw_transport_peer_id_equal(&a, &b));
    assert(!spw_transport_peer_id_set(&a, bytes, sizeof(bytes)));
    assert(!spw_transport_peer_id_set(NULL, bytes, 1u));
}

static void test_runtime_contract(void) {
    fake_runtime_t state = {100u, 0u};
    const spw_vspw_runtime_ops_t ops = {fake_now_us, fake_delay_us};
    spw_vspw_runtime_t runtime = {&ops, &state};
    spw_vspw_runtime_t invalid = SPW_VSPW_RUNTIME_INITIALIZER;

    assert(spw_vspw_runtime_valid(&runtime));
    assert(spw_vspw_runtime_now_us(&runtime) == 100u);
    assert(spw_vspw_runtime_delay_us(&runtime, 20u, 19u) == SPW_ERR_TIMEOUT);
    assert(state.now_us == 100u);
    assert(spw_vspw_runtime_delay_us(&runtime, 20u, 20u) == SPW_OK);
    assert(state.now_us == 120u);
    assert(state.delayed_us == 20u);
    assert(spw_vspw_runtime_delay_us(&runtime, 5u, SPW_TIMEOUT_INFINITE) == SPW_OK);
    assert(state.now_us == 125u);
    assert(spw_vspw_runtime_now_us(&invalid) == 0u);
    assert(spw_vspw_runtime_delay_us(&invalid, 1u, 1u) ==
           SPW_ERR_INVALID_ARGUMENT);
}

static void test_memory_transport(void) {
    static spw_in_memory_transport_link_t link;
    spw_in_memory_transport_endpoint_t endpoint_a;
    spw_in_memory_transport_endpoint_t endpoint_b;
    spw_transport_provider_t provider_a = SPW_TRANSPORT_PROVIDER_INITIALIZER;
    spw_transport_provider_t provider_b = SPW_TRANSPORT_PROVIDER_INITIALIZER;
    spw_transport_peer_id_t peer_a = SPW_TRANSPORT_PEER_ID_INITIALIZER;
    spw_transport_peer_id_t peer_b = SPW_TRANSPORT_PEER_ID_INITIALIZER;
    spw_transport_peer_id_t peer_rx = SPW_TRANSPORT_PEER_ID_INITIALIZER;
    spw_transport_peer_id_t wrong_peer = SPW_TRANSPORT_PEER_ID_INITIALIZER;
    const uint8_t payload[] = {0x10u, 0x20u, 0x30u, 0x40u};
    uint8_t receive_buffer[16] = {0};
    size_t received_size = 0u;
    size_t mtu = 0u;
    spw_transport_state_t state = SPW_TRANSPORT_STATE_DOWN;
    spw_transport_ready_t ready = SPW_TRANSPORT_READY_NONE;
    size_t i;

    spw_in_memory_transport_link_init(&link);
    make_peer(&peer_a, 0xa1u);
    make_peer(&peer_b, 0xb2u);
    make_peer(&wrong_peer, 0xeeu);

    assert(spw_in_memory_transport_endpoint_init(
               &endpoint_a, &link, 0u, &peer_a, &peer_b) == SPW_OK);
    assert(spw_in_memory_transport_endpoint_init(
               &endpoint_b, &link, 1u, &peer_b, &peer_a) == SPW_OK);
    spw_in_memory_transport_provider(&endpoint_a, &provider_a);
    spw_in_memory_transport_provider(&endpoint_b, &provider_b);

    assert(spw_transport_provider_valid(&provider_a));
    assert(spw_transport_provider_valid(&provider_b));
    assert(spw_transport_provider_get_mtu(&provider_a, &mtu) == SPW_OK);
    assert(mtu == SPW_IN_MEMORY_TRANSPORT_MAX_MESSAGE_SIZE);
    assert(spw_transport_provider_get_state(&provider_a, &state) == SPW_OK);
    assert(state == SPW_TRANSPORT_STATE_DOWN);
    assert(spw_transport_provider_send(&provider_a, &peer_b, payload,
                                       sizeof(payload), SPW_TIMEOUT_IMMEDIATE) ==
           SPW_ERR_INVALID_STATE);

    assert(spw_transport_provider_start(&provider_a) == SPW_OK);
    assert(spw_transport_provider_start(&provider_b) == SPW_OK);
    assert(spw_transport_provider_get_state(&provider_a, &state) == SPW_OK);
    assert(state == SPW_TRANSPORT_STATE_UP);
    assert(spw_transport_provider_wait(&provider_a, SPW_TRANSPORT_READY_TX,
                                       SPW_TIMEOUT_IMMEDIATE, &ready) == SPW_OK);
    assert(ready == SPW_TRANSPORT_READY_TX);
    assert(spw_transport_provider_wait(&provider_b, SPW_TRANSPORT_READY_RX,
                                       SPW_TIMEOUT_IMMEDIATE, &ready) ==
           SPW_ERR_TIMEOUT);
    assert(ready == SPW_TRANSPORT_READY_NONE);

    assert(spw_transport_provider_send(&provider_a, &wrong_peer, payload,
                                       sizeof(payload), SPW_TIMEOUT_IMMEDIATE) ==
           SPW_ERR_LINK_UNAVAILABLE);
    assert(spw_transport_provider_send(&provider_a, &peer_b, payload,
                                       sizeof(payload), SPW_TIMEOUT_IMMEDIATE) ==
           SPW_OK);
    assert(spw_transport_provider_wait(&provider_b, SPW_TRANSPORT_READY_RX,
                                       SPW_TIMEOUT_IMMEDIATE, &ready) == SPW_OK);
    assert(ready == SPW_TRANSPORT_READY_RX);

    assert(spw_transport_provider_receive(&provider_b, receive_buffer, 2u,
                                          &received_size, &peer_rx,
                                          SPW_TIMEOUT_IMMEDIATE) ==
           SPW_ERR_BUFFER_TOO_SMALL);
    assert(received_size == sizeof(payload));
    assert(spw_transport_peer_id_equal(&peer_rx, &peer_a));

    memset(receive_buffer, 0, sizeof(receive_buffer));
    received_size = 0u;
    assert(spw_transport_provider_receive(
               &provider_b, receive_buffer, sizeof(receive_buffer),
               &received_size, &peer_rx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(received_size == sizeof(payload));
    assert(memcmp(receive_buffer, payload, sizeof(payload)) == 0);
    assert(spw_transport_peer_id_equal(&peer_rx, &peer_a));

    assert(spw_transport_provider_send(&provider_a, &peer_b, NULL, 0u,
                                       SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(spw_transport_provider_receive(&provider_b, NULL, 0u, &received_size,
                                          &peer_rx, SPW_TIMEOUT_IMMEDIATE) ==
           SPW_OK);
    assert(received_size == 0u);

    for (i = 0u; i < SPW_IN_MEMORY_TRANSPORT_QUEUE_DEPTH; ++i) {
        assert(spw_transport_provider_send(&provider_a, &peer_b, payload,
                                           sizeof(payload),
                                           SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    }
    assert(spw_transport_provider_wait(&provider_a, SPW_TRANSPORT_READY_TX,
                                       SPW_TIMEOUT_IMMEDIATE, &ready) ==
           SPW_ERR_TIMEOUT);
    assert(spw_transport_provider_send(&provider_a, &peer_b, payload,
                                       sizeof(payload), SPW_TIMEOUT_IMMEDIATE) ==
           SPW_ERR_RESOURCE_EXHAUSTED);

    assert(spw_transport_provider_reset(&provider_b) == SPW_OK);
    assert(spw_transport_provider_wait(&provider_b, SPW_TRANSPORT_READY_RX,
                                       SPW_TIMEOUT_IMMEDIATE, &ready) ==
           SPW_ERR_TIMEOUT);
    assert(spw_transport_provider_wait(&provider_a, SPW_TRANSPORT_READY_TX,
                                       SPW_TIMEOUT_IMMEDIATE, &ready) == SPW_OK);

    assert(spw_transport_provider_stop(&provider_a) == SPW_OK);
    assert(spw_transport_provider_get_state(&provider_a, &state) == SPW_OK);
    assert(state == SPW_TRANSPORT_STATE_DOWN);
}

int main(void) {
    test_peer_identity();
    test_runtime_contract();
    test_memory_transport();
    return 0;
}
