// SPDX-License-Identifier: Apache-2.0

#include <spwkit/port.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

static spw_port_t* open_loopback(void) {
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
    spw_port_t* port = NULL;
    assert(spw_port_open(&config, &port) == SPW_OK);
    assert(port != NULL);
    return port;
}

static void test_lifecycle_and_capabilities(void) {
    spw_port_t* port = open_loopback();
    spw_link_state_t state = 0xffu;
    spw_capabilities_t caps = {0};

    assert(spw_port_get_link_state(port, &state) == SPW_OK);
    assert(state == SPW_LINK_ERROR_RESET);

    assert(spw_port_get_capabilities(port, &caps) == SPW_OK);
    assert((caps.bits & SPW_CAP_EEP) != 0u);
    assert((caps.bits & SPW_CAP_TIME_CODE) != 0u);
    assert((caps.bits & SPW_CAP_LINK_CONTROL) != 0u);
    assert((caps.bits & SPW_CAP_STATISTICS) != 0u);
    assert((caps.bits & SPW_CAP_ZERO_COPY) == 0u);
    assert(caps.max_packet_size == 4096u);
    assert(caps.tx_queue_depth == 8u);
    assert(caps.rx_queue_depth == 8u);

    assert(spw_port_start(port) == SPW_OK);
    assert(spw_port_get_link_state(port, &state) == SPW_OK);
    assert(state == SPW_LINK_RUN);
    assert(spw_port_stop(port) == SPW_OK);
    assert(spw_port_get_link_state(port, &state) == SPW_OK);
    assert(state == SPW_LINK_READY);
    assert(spw_port_reset(port) == SPW_OK);
    assert(spw_port_get_link_state(port, &state) == SPW_OK);
    assert(state == SPW_LINK_ERROR_RESET);
    assert(spw_port_close(port) == SPW_OK);
}

static void test_packet_loopback_and_truncation_retention(void) {
    spw_port_t* port = open_loopback();
    uint8_t tx[5] = {1u, 2u, 3u, 4u, 5u};
    uint8_t small[2] = {0};
    uint8_t rx[5] = {0};
    spw_packet_t send_packet = {tx, sizeof(tx), sizeof(tx), SPW_TERMINATOR_EEP};
    spw_packet_t small_receive = {small, 0u, sizeof(small), SPW_TERMINATOR_EOP};
    spw_packet_t receive_packet = {rx, 0u, sizeof(rx), SPW_TERMINATOR_EOP};
    spw_packet_t zero_packet = {NULL, 0u, 0u, SPW_TERMINATOR_EOP};

    assert(spw_port_start(port) == SPW_OK);
    assert(spw_port_send(port, &send_packet, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(spw_port_receive(port, &small_receive, SPW_TIMEOUT_IMMEDIATE) ==
           SPW_ERR_BUFFER_TOO_SMALL);
    assert(small_receive.length == sizeof(tx));
    assert(small_receive.terminator == SPW_TERMINATOR_EEP);

    assert(spw_port_receive(port, &receive_packet, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(receive_packet.length == sizeof(tx));
    assert(receive_packet.terminator == SPW_TERMINATOR_EEP);
    assert(memcmp(rx, tx, sizeof(tx)) == 0);

    assert(spw_port_send(port, &zero_packet, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(spw_port_receive(port, &zero_packet, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(zero_packet.length == 0u);
    assert(zero_packet.terminator == SPW_TERMINATOR_EOP);
    assert(spw_port_close(port) == SPW_OK);
}

static void test_packet_queue_exhaustion(void) {
    spw_port_t* port = open_loopback();
    uint8_t byte = 0x5au;
    spw_packet_t packet = {&byte, 1u, 1u, SPW_TERMINATOR_EOP};
    size_t i;

    assert(spw_port_start(port) == SPW_OK);
    for (i = 0u; i < 8u; ++i) {
        assert(spw_port_send(port, &packet, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    }
    assert(spw_port_send(port, &packet, SPW_TIMEOUT_IMMEDIATE) ==
           SPW_ERR_RESOURCE_EXHAUSTED);
    assert(spw_port_close(port) == SPW_OK);
}

static void test_time_codes_and_statistics(void) {
    spw_port_t* port = open_loopback();
    spw_time_code_t invalid = {64u, 0u};
    spw_time_code_t sent = {42u, 0u};
    spw_time_code_t received = {0};
    uint8_t bytes[3] = {9u, 8u, 7u};
    uint8_t output[3] = {0};
    spw_packet_t tx = {bytes, sizeof(bytes), sizeof(bytes), SPW_TERMINATOR_EOP};
    spw_packet_t rx = {output, 0u, sizeof(output), SPW_TERMINATOR_EOP};
    spw_statistics_t stats = {0};

    assert(spw_port_start(port) == SPW_OK);
    assert(spw_port_send_time_code(port, &invalid, SPW_TIMEOUT_IMMEDIATE) ==
           SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_send_time_code(port, &sent, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(spw_port_receive_time_code(port, &received, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(received.time_count == 42u);
    assert(received.control_flags == 0u);

    assert(spw_port_send(port, &tx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(spw_port_receive(port, &rx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);

    assert(spw_port_get_statistics(port, &stats) == SPW_OK);
    assert(stats.tx_packets == 1u);
    assert(stats.rx_packets == 1u);
    assert(stats.tx_bytes == 3u);
    assert(stats.rx_bytes == 3u);
    assert(stats.tx_time_codes == 1u);
    assert(stats.rx_time_codes == 1u);

    assert(spw_port_clear_statistics(port) == SPW_OK);
    assert(spw_port_get_statistics(port, &stats) == SPW_OK);
    assert(stats.tx_packets == 0u);
    assert(stats.rx_packets == 0u);
    assert(spw_port_close(port) == SPW_OK);
}

static void test_reset_clears_pending_data(void) {
    spw_port_t* port = open_loopback();
    uint8_t value = 0x33u;
    uint8_t out = 0u;
    spw_packet_t tx = {&value, 1u, 1u, SPW_TERMINATOR_EOP};
    spw_packet_t rx = {&out, 0u, 1u, SPW_TERMINATOR_EOP};

    assert(spw_port_start(port) == SPW_OK);
    assert(spw_port_send(port, &tx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(spw_port_reset(port) == SPW_OK);
    assert(spw_port_start(port) == SPW_OK);
    assert(spw_port_receive(port, &rx, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_TIMEOUT);
    assert(spw_port_close(port) == SPW_OK);
}

int main(void) {
    test_lifecycle_and_capabilities();
    test_packet_loopback_and_truncation_retention();
    test_packet_queue_exhaustion();
    test_time_codes_and_statistics();
    test_reset_clears_pending_data();
    return 0;
}
