// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <thread>

namespace {

spw_port_t* open_endpoint(std::uint64_t link_id, spw_simulator_endpoint_t endpoint) {
    spw_simulator_config_t simulator = SPW_SIMULATOR_CONFIG_INITIALIZER;
    simulator.link_id = link_id;
    simulator.endpoint = endpoint;

    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
    config.backend_config = &simulator;
    config.backend_config_size = sizeof(simulator);

    spw_port_t* port = nullptr;
    assert(spw_port_open(&config, &port) == SPW_OK);
    assert(port != nullptr);
    return port;
}

void assert_state(spw_port_t* port, spw_link_state_t expected) {
    spw_link_state_t state = 0xffu;
    assert(spw_port_get_link_state(port, &state) == SPW_OK);
    assert(state == expected);
}

void send_bytes(spw_port_t* port,
                std::uint8_t* bytes,
                std::size_t length,
                spw_terminator_t terminator,
                spw_timeout_us_t timeout = SPW_TIMEOUT_IMMEDIATE) {
    spw_packet_t packet{bytes, length, length, terminator};
    assert(spw_port_send(port, &packet, timeout) == SPW_OK);
}

void receive_bytes(spw_port_t* port,
                   std::uint8_t* bytes,
                   std::size_t capacity,
                   std::size_t expected_length,
                   spw_terminator_t expected_terminator,
                   spw_timeout_us_t timeout = SPW_TIMEOUT_IMMEDIATE) {
    spw_packet_t packet{bytes, 0u, capacity, SPW_TERMINATOR_EOP};
    assert(spw_port_receive(port, &packet, timeout) == SPW_OK);
    assert(packet.length == expected_length);
    assert(packet.terminator == expected_terminator);
}

} // namespace

int main() {
    constexpr std::uint64_t link_id = 0x5350574bull;

    spw_port_t* a = open_endpoint(link_id, SPW_SIMULATOR_ENDPOINT_A);
    spw_port_t* b = open_endpoint(link_id, SPW_SIMULATOR_ENDPOINT_B);

    // A link identifier has exactly one A and one B endpoint attached at once.
    spw_simulator_config_t duplicate_sim = SPW_SIMULATOR_CONFIG_INITIALIZER;
    duplicate_sim.link_id = link_id;
    duplicate_sim.endpoint = SPW_SIMULATOR_ENDPOINT_A;
    spw_port_config_t duplicate_config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
    duplicate_config.backend_config = &duplicate_sim;
    duplicate_config.backend_config_size = sizeof(duplicate_sim);
    spw_port_t* duplicate = nullptr;
    assert(spw_port_open(&duplicate_config, &duplicate) == SPW_ERR_RESOURCE_EXHAUSTED);
    assert(duplicate == nullptr);

    assert_state(a, SPW_LINK_ERROR_RESET);
    assert_state(b, SPW_LINK_ERROR_RESET);

    assert(spw_port_start(a) == SPW_OK);
    assert_state(a, SPW_LINK_CONNECTING);
    assert(spw_port_start(b) == SPW_OK);
    assert_state(a, SPW_LINK_RUN);
    assert_state(b, SPW_LINK_RUN);

    std::uint8_t a_to_b[] = {0x41u, 0x2du, 0x3eu, 0x42u};
    std::uint8_t b_to_a[] = {0x42u, 0x2du, 0x3eu, 0x41u};
    send_bytes(a, a_to_b, sizeof(a_to_b), SPW_TERMINATOR_EOP);
    send_bytes(b, b_to_a, sizeof(b_to_a), SPW_TERMINATOR_EEP);

    std::array<std::uint8_t, 16> rx_b{};
    std::array<std::uint8_t, 16> rx_a{};
    receive_bytes(b, rx_b.data(), rx_b.size(), sizeof(a_to_b), SPW_TERMINATOR_EOP);
    receive_bytes(a, rx_a.data(), rx_a.size(), sizeof(b_to_a), SPW_TERMINATOR_EEP);
    assert(std::memcmp(rx_b.data(), a_to_b, sizeof(a_to_b)) == 0);
    assert(std::memcmp(rx_a.data(), b_to_a, sizeof(b_to_a)) == 0);

    // An undersized read reports the required size without consuming the packet.
    std::uint8_t larger[] = {0u, 1u, 2u, 3u, 4u, 5u};
    send_bytes(a, larger, sizeof(larger), SPW_TERMINATOR_EOP);
    std::array<std::uint8_t, 2> tiny{};
    spw_packet_t short_rx{tiny.data(), 0u, tiny.size(), SPW_TERMINATOR_EEP};
    assert(spw_port_receive(b, &short_rx, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_BUFFER_TOO_SMALL);
    assert(short_rx.length == sizeof(larger));
    std::array<std::uint8_t, sizeof(larger)> full{};
    receive_bytes(b, full.data(), full.size(), sizeof(larger), SPW_TERMINATOR_EOP);
    assert(std::memcmp(full.data(), larger, sizeof(larger)) == 0);

    // The receiving endpoint owns a bounded queue.
    std::uint8_t queued = 0x5au;
    spw_packet_t queued_packet{&queued, 1u, 1u, SPW_TERMINATOR_EOP};
    for (std::size_t i = 0; i < 8u; ++i) {
        assert(spw_port_send(a, &queued_packet, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    }
    assert(spw_port_send(a, &queued_packet, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_RESOURCE_EXHAUSTED);
    for (std::size_t i = 0; i < 8u; ++i) {
        std::uint8_t value = 0u;
        receive_bytes(b, &value, 1u, 1u, SPW_TERMINATOR_EOP);
        assert(value == queued);
    }

    spw_time_code_t tx_time{17u, 0u};
    spw_time_code_t rx_time{0u, 0u};
    assert(spw_port_send_time_code(a, &tx_time, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(spw_port_receive_time_code(b, &rx_time, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(rx_time.time_count == tx_time.time_count);
    assert(rx_time.control_flags == tx_time.control_flags);

    // Exercise simultaneous full-duplex traffic through the public API.
    constexpr std::size_t iterations = 64u;
    std::thread a_worker([&] {
        for (std::size_t i = 0; i < iterations; ++i) {
            std::uint8_t tx = static_cast<std::uint8_t>(i);
            spw_packet_t packet{&tx, 1u, 1u, SPW_TERMINATOR_EOP};
            assert(spw_port_send(a, &packet, SPW_TIMEOUT_INFINITE) == SPW_OK);

            std::uint8_t rx = 0u;
            spw_packet_t received{&rx, 0u, 1u, SPW_TERMINATOR_EOP};
            assert(spw_port_receive(a, &received, SPW_TIMEOUT_INFINITE) == SPW_OK);
            assert(rx == static_cast<std::uint8_t>(0x80u + i));
        }
    });

    std::thread b_worker([&] {
        for (std::size_t i = 0; i < iterations; ++i) {
            std::uint8_t tx = static_cast<std::uint8_t>(0x80u + i);
            spw_packet_t packet{&tx, 1u, 1u, SPW_TERMINATOR_EOP};
            assert(spw_port_send(b, &packet, SPW_TIMEOUT_INFINITE) == SPW_OK);

            std::uint8_t rx = 0u;
            spw_packet_t received{&rx, 0u, 1u, SPW_TERMINATOR_EOP};
            assert(spw_port_receive(b, &received, SPW_TIMEOUT_INFINITE) == SPW_OK);
            assert(rx == static_cast<std::uint8_t>(i));
        }
    });

    a_worker.join();
    b_worker.join();

    // A peer reset/disconnect is visible to the surviving endpoint and recovery
    // does not require replacing the surviving application handle.
    assert(spw_port_reset(b) == SPW_OK);
    assert_state(b, SPW_LINK_ERROR_RESET);
    assert_state(a, SPW_LINK_CONNECTING);
    spw_packet_t unavailable{a_to_b, sizeof(a_to_b), sizeof(a_to_b), SPW_TERMINATOR_EOP};
    assert(spw_port_send(a, &unavailable, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_LINK_UNAVAILABLE);

    assert(spw_port_start(b) == SPW_OK);
    assert_state(a, SPW_LINK_RUN);
    assert_state(b, SPW_LINK_RUN);

    assert(spw_port_close(b) == SPW_OK);
    b = nullptr;
    assert_state(a, SPW_LINK_CONNECTING);
    assert(spw_port_send(a, &unavailable, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_LINK_UNAVAILABLE);

    b = open_endpoint(link_id, SPW_SIMULATOR_ENDPOINT_B);
    assert(spw_port_start(b) == SPW_OK);
    assert_state(a, SPW_LINK_RUN);
    assert_state(b, SPW_LINK_RUN);

    send_bytes(a, a_to_b, sizeof(a_to_b), SPW_TERMINATOR_EOP);
    rx_b.fill(0u);
    receive_bytes(b, rx_b.data(), rx_b.size(), sizeof(a_to_b), SPW_TERMINATOR_EOP);
    assert(std::memcmp(rx_b.data(), a_to_b, sizeof(a_to_b)) == 0);

    spw_statistics_t a_stats{};
    spw_statistics_t b_stats{};
    assert(spw_port_get_statistics(a, &a_stats) == SPW_OK);
    assert(spw_port_get_statistics(b, &b_stats) == SPW_OK);
    assert(a_stats.tx_packets > 0u);
    assert(a_stats.rx_packets > 0u);
    assert(a_stats.link_errors > 0u);
    assert(b_stats.rx_packets > 0u);

    assert(spw_port_close(b) == SPW_OK);
    assert(spw_port_close(a) == SPW_OK);
    return 0;
}
