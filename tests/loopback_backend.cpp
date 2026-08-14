// SPDX-License-Identifier: Apache-2.0

#include "backends/loopback/loopback_backend.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

using spwkit::detail::LoopbackBackend;

static void test_lifecycle_and_capabilities() {
    LoopbackBackend backend;
    spw_link_state_t state = 0xffu;
    assert(backend.get_link_state(state) == SPW_OK);
    assert(state == SPW_LINK_ERROR_RESET);

    spw_capabilities_t caps{};
    assert(backend.get_capabilities(caps) == SPW_OK);
    assert((caps.bits & SPW_CAP_EEP) != 0u);
    assert((caps.bits & SPW_CAP_TIME_CODE) != 0u);
    assert((caps.bits & SPW_CAP_LINK_CONTROL) != 0u);
    assert((caps.bits & SPW_CAP_STATISTICS) != 0u);
    assert((caps.bits & SPW_CAP_ZERO_COPY) == 0u);
    assert(caps.max_packet_size == LoopbackBackend::max_packet_size);
    assert(caps.tx_queue_depth == LoopbackBackend::packet_queue_depth);
    assert(caps.rx_queue_depth == LoopbackBackend::packet_queue_depth);

    assert(backend.start() == SPW_OK);
    assert(backend.get_link_state(state) == SPW_OK);
    assert(state == SPW_LINK_RUN);
    assert(backend.stop() == SPW_OK);
    assert(backend.get_link_state(state) == SPW_OK);
    assert(state == SPW_LINK_READY);
    assert(backend.reset() == SPW_OK);
    assert(backend.get_link_state(state) == SPW_OK);
    assert(state == SPW_LINK_ERROR_RESET);
}

static void test_packet_loopback_and_truncation_retention() {
    LoopbackBackend backend;
    assert(backend.start() == SPW_OK);

    std::array<std::uint8_t, 5> tx{{1u, 2u, 3u, 4u, 5u}};
    spw_packet_t send_packet{tx.data(), tx.size(), tx.size(), SPW_TERMINATOR_EEP};
    assert(backend.send(send_packet, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);

    std::array<std::uint8_t, 2> small{};
    spw_packet_t small_receive{small.data(), 0u, small.size(), SPW_TERMINATOR_EOP};
    assert(backend.receive(small_receive, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_BUFFER_TOO_SMALL);
    assert(small_receive.length == tx.size());
    assert(small_receive.terminator == SPW_TERMINATOR_EEP);

    std::array<std::uint8_t, 5> rx{};
    spw_packet_t receive_packet{rx.data(), 0u, rx.size(), SPW_TERMINATOR_EOP};
    assert(backend.receive(receive_packet, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(receive_packet.length == tx.size());
    assert(receive_packet.terminator == SPW_TERMINATOR_EEP);
    assert(std::memcmp(rx.data(), tx.data(), tx.size()) == 0);

    spw_packet_t zero_packet{nullptr, 0u, 0u, SPW_TERMINATOR_EOP};
    assert(backend.send(zero_packet, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(backend.receive(zero_packet, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(zero_packet.length == 0u);
    assert(zero_packet.terminator == SPW_TERMINATOR_EOP);
}

static void test_packet_queue_exhaustion() {
    LoopbackBackend backend;
    assert(backend.start() == SPW_OK);

    std::uint8_t byte = 0x5au;
    spw_packet_t packet{&byte, 1u, 1u, SPW_TERMINATOR_EOP};
    for (std::size_t i = 0; i < LoopbackBackend::packet_queue_depth; ++i) {
        assert(backend.send(packet, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    }
    assert(backend.send(packet, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_RESOURCE_EXHAUSTED);
}

static void test_time_codes_and_statistics() {
    LoopbackBackend backend;
    assert(backend.start() == SPW_OK);

    spw_time_code_t invalid{64u, 0u};
    assert(backend.send_time_code(invalid, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_INVALID_ARGUMENT);

    spw_time_code_t sent{42u, 0u};
    assert(backend.send_time_code(sent, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    spw_time_code_t received{};
    assert(backend.receive_time_code(received, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(received.time_count == 42u);
    assert(received.control_flags == 0u);

    std::array<std::uint8_t, 3> bytes{{9u, 8u, 7u}};
    spw_packet_t tx{bytes.data(), bytes.size(), bytes.size(), SPW_TERMINATOR_EOP};
    assert(backend.send(tx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    std::array<std::uint8_t, 3> output{};
    spw_packet_t rx{output.data(), 0u, output.size(), SPW_TERMINATOR_EOP};
    assert(backend.receive(rx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);

    spw_statistics_t stats{};
    assert(backend.get_statistics(stats) == SPW_OK);
    assert(stats.tx_packets == 1u);
    assert(stats.rx_packets == 1u);
    assert(stats.tx_bytes == 3u);
    assert(stats.rx_bytes == 3u);
    assert(stats.tx_time_codes == 1u);
    assert(stats.rx_time_codes == 1u);

    assert(backend.clear_statistics() == SPW_OK);
    assert(backend.get_statistics(stats) == SPW_OK);
    assert(stats.tx_packets == 0u);
    assert(stats.rx_packets == 0u);
}

static void test_reset_clears_pending_data() {
    LoopbackBackend backend;
    assert(backend.start() == SPW_OK);

    std::uint8_t value = 0x33u;
    spw_packet_t tx{&value, 1u, 1u, SPW_TERMINATOR_EOP};
    assert(backend.send(tx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(backend.reset() == SPW_OK);
    assert(backend.start() == SPW_OK);

    std::uint8_t out = 0u;
    spw_packet_t rx{&out, 0u, 1u, SPW_TERMINATOR_EOP};
    assert(backend.receive(rx, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_TIMEOUT);
}

int main() {
    test_lifecycle_and_capabilities();
    test_packet_loopback_and_truncation_retention();
    test_packet_queue_exhaustion();
    test_time_codes_and_statistics();
    test_reset_clears_pending_data();
    return 0;
}
