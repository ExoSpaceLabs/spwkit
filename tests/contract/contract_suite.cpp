// SPDX-License-Identifier: Apache-2.0

#include "contract_suite.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <vector>

namespace spwkit::test {
namespace {

spw_timeout_us_t g_transfer_timeout_us = SPW_TIMEOUT_IMMEDIATE;

[[noreturn]] void fail(const char* test, const char* message) {
    std::cerr << "[contract][FAIL] " << test << ": " << message << '\n';
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* test, const char* message) {
    if (!condition) {
        fail(test, message);
    }
}

void require_result(spw_result_t actual,
                    spw_result_t expected,
                    const char* test,
                    const char* message) {
    if (actual != expected) {
        std::cerr << "[contract] expected result " << expected
                  << ", got " << actual << '\n';
        fail(test, message);
    }
}

spw_link_state_t link_state(spw_port_t* port, const char* test) {
    spw_link_state_t state = 0xffu;
    require_result(spw_port_get_link_state(port, &state), SPW_OK, test,
                   "failed to query link state");
    return state;
}

spw_capabilities_t capabilities(spw_port_t* port, const char* test) {
    spw_capabilities_t caps{};
    require_result(spw_port_get_capabilities(port, &caps), SPW_OK, test,
                   "failed to query capabilities");
    return caps;
}

void require_running(BackendContractFixture& fixture, const char* test) {
    require(link_state(fixture.endpoint_a(), test) == SPW_LINK_RUN, test,
            "endpoint A is not in RUN");
    require(link_state(fixture.endpoint_b(), test) == SPW_LINK_RUN, test,
            "endpoint B is not in RUN");
}

void prepare_running(BackendContractFixture& fixture, const char* test) {
    fixture.reset_link();
    fixture.start_link();
    require_running(fixture, test);
}

void send_packet(spw_port_t* port,
                 std::uint8_t* data,
                 std::size_t length,
                 spw_terminator_t terminator,
                 const char* test) {
    spw_packet_t packet{data, length, length, terminator};
    require_result(spw_port_send(port, &packet, g_transfer_timeout_us), SPW_OK,
                   test, "packet send failed");
}

void receive_packet(spw_port_t* port,
                    std::uint8_t* data,
                    std::size_t capacity,
                    std::size_t expected_length,
                    spw_terminator_t expected_terminator,
                    const char* test) {
    spw_packet_t packet{data, 0u, capacity, SPW_TERMINATOR_EOP};
    require_result(spw_port_receive(port, &packet, g_transfer_timeout_us), SPW_OK,
                   test, "packet receive failed");
    require(packet.length == expected_length, test, "received length mismatch");
    require(packet.terminator == expected_terminator, test,
            "received terminator mismatch");
}

void test_lifecycle(BackendContractFixture& fixture) {
    constexpr const char* test = "lifecycle";

    require(link_state(fixture.endpoint_a(), test) == SPW_LINK_ERROR_RESET, test,
            "endpoint A must open in ERROR_RESET");
    require(link_state(fixture.endpoint_b(), test) == SPW_LINK_ERROR_RESET, test,
            "endpoint B must open in ERROR_RESET");

    fixture.start_link();
    require_running(fixture, test);

    fixture.stop_link();
    require(link_state(fixture.endpoint_a(), test) != SPW_LINK_RUN, test,
            "endpoint A remained in RUN after stop");
    require(link_state(fixture.endpoint_b(), test) != SPW_LINK_RUN, test,
            "endpoint B remained in RUN after stop");

    fixture.reset_link();
    require(link_state(fixture.endpoint_a(), test) == SPW_LINK_ERROR_RESET, test,
            "endpoint A did not return to ERROR_RESET");
    require(link_state(fixture.endpoint_b(), test) == SPW_LINK_ERROR_RESET, test,
            "endpoint B did not return to ERROR_RESET");

    fixture.start_link();
    require_running(fixture, test);
}

void test_bidirectional_packets(BackendContractFixture& fixture,
                                const spw_capabilities_t& caps_a,
                                const spw_capabilities_t& caps_b) {
    constexpr const char* test = "bidirectional-packets";
    prepare_running(fixture, test);

    std::uint8_t a_to_b[] = {0x53u, 0x50u, 0x57u, 0x41u};
    std::uint8_t b_to_a[] = {0x53u, 0x50u, 0x57u, 0x42u};
    std::uint8_t rx[sizeof(a_to_b)]{};

    send_packet(fixture.endpoint_a(), a_to_b, sizeof(a_to_b), SPW_TERMINATOR_EOP, test);
    receive_packet(fixture.endpoint_b(), rx, sizeof(rx), sizeof(a_to_b),
                   SPW_TERMINATOR_EOP, test);
    require(std::memcmp(rx, a_to_b, sizeof(a_to_b)) == 0, test,
            "A->B payload mismatch");

    const bool eep_supported =
        (caps_a.bits & SPW_CAP_EEP) != 0u && (caps_b.bits & SPW_CAP_EEP) != 0u;
    const spw_terminator_t reverse_terminator =
        eep_supported ? SPW_TERMINATOR_EEP : SPW_TERMINATOR_EOP;

    std::fill(std::begin(rx), std::end(rx), 0u);
    send_packet(fixture.endpoint_b(), b_to_a, sizeof(b_to_a), reverse_terminator, test);
    receive_packet(fixture.endpoint_a(), rx, sizeof(rx), sizeof(b_to_a),
                   reverse_terminator, test);
    require(std::memcmp(rx, b_to_a, sizeof(b_to_a)) == 0, test,
            "B->A payload mismatch");
}

void test_zero_length_packet(BackendContractFixture& fixture) {
    constexpr const char* test = "zero-length-packet";
    prepare_running(fixture, test);

    spw_packet_t tx{nullptr, 0u, 0u, SPW_TERMINATOR_EOP};
    require_result(spw_port_send(fixture.endpoint_a(), &tx, g_transfer_timeout_us),
                   SPW_OK, test, "zero-length send failed");

    spw_packet_t rx{nullptr, 0u, 0u, SPW_TERMINATOR_EEP};
    require_result(spw_port_receive(fixture.endpoint_b(), &rx, g_transfer_timeout_us),
                   SPW_OK, test, "zero-length receive failed");
    require(rx.length == 0u, test, "zero-length packet acquired non-zero length");
    require(rx.terminator == SPW_TERMINATOR_EOP, test,
            "zero-length packet terminator changed");
}

void test_large_packet(BackendContractFixture& fixture,
                       const spw_capabilities_t& caps_a,
                       const spw_capabilities_t& caps_b) {
    constexpr const char* test = "large-packet";
    prepare_running(fixture, test);

    std::size_t length = 4096u;
    if (caps_a.max_packet_size != 0u) {
        length = std::min(length, caps_a.max_packet_size);
    }
    if (caps_b.max_packet_size != 0u) {
        length = std::min(length, caps_b.max_packet_size);
    }
    require(length > 0u, test, "backend advertises no transferable packet size");

    std::vector<std::uint8_t> tx(length);
    std::vector<std::uint8_t> rx(length, 0u);
    for (std::size_t i = 0; i < tx.size(); ++i) {
        tx[i] = static_cast<std::uint8_t>((i * 37u + 11u) & 0xffu);
    }

    send_packet(fixture.endpoint_a(), tx.data(), tx.size(), SPW_TERMINATOR_EOP, test);
    receive_packet(fixture.endpoint_b(), rx.data(), rx.size(), tx.size(),
                   SPW_TERMINATOR_EOP, test);
    require(rx == tx, test, "large packet payload mismatch");
}

void test_receive_capacity_retention(BackendContractFixture& fixture,
                                     const spw_capabilities_t& caps_a,
                                     const spw_capabilities_t& caps_b) {
    constexpr const char* test = "receive-capacity-retention";
    prepare_running(fixture, test);

    const std::size_t max_size = std::min(
        caps_a.max_packet_size == 0u ? std::size_t{6u} : caps_a.max_packet_size,
        caps_b.max_packet_size == 0u ? std::size_t{6u} : caps_b.max_packet_size);
    if (max_size < 2u) {
        std::cout << "[contract][SKIP] " << test
                  << ": advertised max packet size is below 2 bytes\n";
        return;
    }

    const std::size_t length = std::min<std::size_t>(6u, max_size);
    std::vector<std::uint8_t> tx(length);
    for (std::size_t i = 0; i < length; ++i) {
        tx[i] = static_cast<std::uint8_t>(0xa0u + i);
    }

    send_packet(fixture.endpoint_a(), tx.data(), tx.size(), SPW_TERMINATOR_EEP, test);

    std::uint8_t tiny = 0u;
    spw_packet_t short_rx{&tiny, 0u, 1u, SPW_TERMINATOR_EOP};
    require_result(spw_port_receive(fixture.endpoint_b(), &short_rx,
                                    g_transfer_timeout_us),
                   SPW_ERR_BUFFER_TOO_SMALL, test,
                   "undersized receive did not report BUFFER_TOO_SMALL");
    require(short_rx.length == tx.size(), test,
            "required receive length was not reported");
    require(short_rx.terminator == SPW_TERMINATOR_EEP, test,
            "terminator was not preserved on BUFFER_TOO_SMALL");

    std::vector<std::uint8_t> rx(length, 0u);
    receive_packet(fixture.endpoint_b(), rx.data(), rx.size(), tx.size(),
                   SPW_TERMINATOR_EEP, test);
    require(rx == tx, test, "packet was consumed or modified after short receive");
}

void test_timeout_and_nonblocking(BackendContractFixture& fixture) {
    constexpr const char* test = "timeout-nonblocking";
    prepare_running(fixture, test);

    std::uint8_t byte = 0u;
    spw_packet_t rx{&byte, 0u, 1u, SPW_TERMINATOR_EOP};
    require_result(spw_port_receive(fixture.endpoint_b(), &rx,
                                    SPW_TIMEOUT_IMMEDIATE),
                   SPW_ERR_TIMEOUT, test,
                   "empty immediate receive did not time out");

    rx.length = 0u;
    require_result(spw_port_receive(fixture.endpoint_b(), &rx, 1000u),
                   SPW_ERR_TIMEOUT, test,
                   "empty finite receive did not time out");
}

void test_bounded_queue(BackendContractFixture& fixture,
                        const spw_capabilities_t& caps_a,
                        const spw_capabilities_t& caps_b) {
    constexpr const char* test = "bounded-queue";
    if (!fixture.has_strict_bounded_queue_contract()) {
        std::cout << "[contract][SKIP] " << test
                  << ": queue depth is transport/service capacity, not an immediate-send guarantee\n";
        return;
    }
    prepare_running(fixture, test);

    std::size_t depth = caps_b.rx_queue_depth;
    if (caps_a.tx_queue_depth != 0u) {
        depth = depth == 0u ? caps_a.tx_queue_depth
                            : std::min(depth, caps_a.tx_queue_depth);
    }
    if (depth == 0u || depth > 1024u) {
        std::cout << "[contract][SKIP] " << test
                  << ": queue depth is unspecified or outside bounded test range\n";
        return;
    }

    std::uint8_t value = 0x5au;
    spw_packet_t tx{&value, 1u, 1u, SPW_TERMINATOR_EOP};
    for (std::size_t i = 0; i < depth; ++i) {
        require_result(spw_port_send(fixture.endpoint_a(), &tx,
                                     SPW_TIMEOUT_IMMEDIATE),
                       SPW_OK, test, "queue filled before advertised depth");
    }

    const spw_result_t full_result =
        spw_port_send(fixture.endpoint_a(), &tx, SPW_TIMEOUT_IMMEDIATE);
    require(full_result == SPW_ERR_RESOURCE_EXHAUSTED ||
                full_result == SPW_ERR_TIMEOUT,
            test, "full queue did not report exhaustion/timeout");

    for (std::size_t i = 0; i < depth; ++i) {
        std::uint8_t received = 0u;
        receive_packet(fixture.endpoint_b(), &received, 1u, 1u,
                       SPW_TERMINATOR_EOP, test);
        require(received == value, test, "queued payload corrupted");
    }

    require_result(spw_port_send(fixture.endpoint_a(), &tx, g_transfer_timeout_us),
                   SPW_OK, test, "queue did not recover after drain");
    std::uint8_t received = 0u;
    receive_packet(fixture.endpoint_b(), &received, 1u, 1u,
                   SPW_TERMINATOR_EOP, test);
}

void test_time_codes(BackendContractFixture& fixture,
                     const spw_capabilities_t& caps_a,
                     const spw_capabilities_t& caps_b) {
    constexpr const char* test = "time-codes";
    if ((caps_a.bits & SPW_CAP_TIME_CODE) == 0u ||
        (caps_b.bits & SPW_CAP_TIME_CODE) == 0u) {
        std::cout << "[contract][SKIP] " << test
                  << ": capability not advertised\n";
        return;
    }

    prepare_running(fixture, test);
    spw_time_code_t tx{37u, 0u};
    spw_time_code_t rx{};
    require_result(spw_port_send_time_code(fixture.endpoint_a(), &tx,
                                           g_transfer_timeout_us),
                   SPW_OK, test, "time-code send failed");
    require_result(spw_port_receive_time_code(fixture.endpoint_b(), &rx,
                                              g_transfer_timeout_us),
                   SPW_OK, test, "time-code receive failed");
    require(rx.time_count == tx.time_count &&
                rx.control_flags == tx.control_flags,
            test, "time-code content mismatch");
}

void test_statistics(BackendContractFixture& fixture,
                     const spw_capabilities_t& caps_a,
                     const spw_capabilities_t& caps_b) {
    constexpr const char* test = "statistics";
    if ((caps_a.bits & SPW_CAP_STATISTICS) == 0u ||
        (caps_b.bits & SPW_CAP_STATISTICS) == 0u) {
        std::cout << "[contract][SKIP] " << test
                  << ": capability not advertised\n";
        return;
    }

    prepare_running(fixture, test);
    require_result(spw_port_clear_statistics(fixture.endpoint_a()), SPW_OK, test,
                   "failed to clear endpoint A statistics");
    require_result(spw_port_clear_statistics(fixture.endpoint_b()), SPW_OK, test,
                   "failed to clear endpoint B statistics");

    std::uint8_t tx_data[] = {9u, 8u, 7u};
    std::uint8_t rx_data[sizeof(tx_data)]{};
    send_packet(fixture.endpoint_a(), tx_data, sizeof(tx_data),
                SPW_TERMINATOR_EOP, test);
    receive_packet(fixture.endpoint_b(), rx_data, sizeof(rx_data),
                   sizeof(tx_data), SPW_TERMINATOR_EOP, test);

    spw_statistics_t stats_a{};
    spw_statistics_t stats_b{};
    require_result(spw_port_get_statistics(fixture.endpoint_a(), &stats_a), SPW_OK,
                   test, "failed to query endpoint A statistics");
    require_result(spw_port_get_statistics(fixture.endpoint_b(), &stats_b), SPW_OK,
                   test, "failed to query endpoint B statistics");
    require(stats_a.tx_packets >= 1u && stats_a.tx_bytes >= sizeof(tx_data), test,
            "TX statistics did not advance");
    require(stats_b.rx_packets >= 1u && stats_b.rx_bytes >= sizeof(tx_data), test,
            "RX statistics did not advance");
}

void test_optional_zero_copy(BackendContractFixture& fixture,
                             const spw_capabilities_t& caps_a,
                             const spw_capabilities_t& caps_b) {
    constexpr const char* test = "zero-copy";
    const bool advertised =
        (caps_a.bits & SPW_CAP_ZERO_COPY) != 0u &&
        (caps_b.bits & SPW_CAP_ZERO_COPY) != 0u;
    if (!advertised) {
        std::cout << "[contract][SKIP] " << test
                  << ": capability not advertised\n";
        return;
    }

    require(fixture.has_zero_copy_contract(), test,
            "backend advertises ZERO_COPY but fixture has no ownership contract");
    fixture.run_zero_copy_contract();
}

void print_profile(const BackendContractFixture& fixture,
                   const spw_capabilities_t& caps_a,
                   const spw_capabilities_t& caps_b) {
    std::cout << "[contract] backend=" << fixture.name()
              << " capsA=0x" << std::hex << caps_a.bits
              << " capsB=0x" << caps_b.bits << std::dec
              << " maxPacketA=" << caps_a.max_packet_size
              << " maxPacketB=" << caps_b.max_packet_size
              << " rxDepthA=" << caps_a.rx_queue_depth
              << " rxDepthB=" << caps_b.rx_queue_depth
              << " transferTimeoutUs=" << fixture.transfer_timeout_us()
              << '\n';
}

} // namespace

int run_backend_contract(BackendContractFixture& fixture) {
    g_transfer_timeout_us = fixture.transfer_timeout_us();
    const spw_capabilities_t caps_a = capabilities(fixture.endpoint_a(), "capabilities");
    const spw_capabilities_t caps_b = capabilities(fixture.endpoint_b(), "capabilities");
    print_profile(fixture, caps_a, caps_b);

    test_lifecycle(fixture);
    test_bidirectional_packets(fixture, caps_a, caps_b);
    test_zero_length_packet(fixture);
    test_large_packet(fixture, caps_a, caps_b);
    test_receive_capacity_retention(fixture, caps_a, caps_b);
    test_timeout_and_nonblocking(fixture);
    test_bounded_queue(fixture, caps_a, caps_b);
    test_time_codes(fixture, caps_a, caps_b);
    test_statistics(fixture, caps_a, caps_b);
    test_optional_zero_copy(fixture, caps_a, caps_b);

    fixture.reset_link();
    std::cout << "[contract][PASS] backend=" << fixture.name() << '\n';
    return EXIT_SUCCESS;
}

} // namespace spwkit::test
