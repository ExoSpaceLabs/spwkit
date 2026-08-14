// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

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

spw_link_state_t state_of(spw_port_t* port) {
    spw_link_state_t state = 0xffu;
    assert(spw_port_get_link_state(port, &state) == SPW_OK);
    return state;
}

} // namespace

int main() {
    /* Configuration boundaries. */
    spw_simulator_config_t simulator = SPW_SIMULATOR_CONFIG_INITIALIZER;
    simulator.link_id = 0x7001u;
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
    config.backend_config = &simulator;
    config.backend_config_size = sizeof(simulator);
    spw_port_t* port = nullptr;

    auto bad_sim = simulator;
    bad_sim.struct_size = sizeof(bad_sim) - 1u;
    config.backend_config = &bad_sim;
    assert(spw_port_open(&config, &port) == SPW_ERR_INVALID_ARGUMENT);
    bad_sim = simulator;
    bad_sim.version = SPW_SIMULATOR_CONFIG_VERSION + 1u;
    config.backend_config = &bad_sim;
    assert(spw_port_open(&config, &port) == SPW_ERR_UNSUPPORTED);
    bad_sim = simulator;
    bad_sim.endpoint = 2u;
    config.backend_config = &bad_sim;
    assert(spw_port_open(&config, &port) == SPW_ERR_INVALID_ARGUMENT);
    config.backend_config = nullptr;
    config.backend_config_size = 0u;
    assert(spw_port_open(&config, &port) == SPW_ERR_INVALID_ARGUMENT);
    config.backend_config = &simulator;
    config.backend_config_size = sizeof(simulator) - 1u;
    assert(spw_port_open(&config, &port) == SPW_ERR_INVALID_ARGUMENT);

    /* One A + one B per link, and a started lone endpoint stays CONNECTING. */
    constexpr std::uint64_t link_id = 0x7002u;
    spw_port_t* a = open_endpoint(link_id, SPW_SIMULATOR_ENDPOINT_A);
    assert(state_of(a) == SPW_LINK_ERROR_RESET);
    assert(spw_port_start(a) == SPW_OK);
    assert(spw_port_start(a) == SPW_OK);
    assert(state_of(a) == SPW_LINK_CONNECTING);

    std::uint8_t byte = 0x44u;
    spw_packet_t one{&byte, 1u, 1u, SPW_TERMINATOR_EOP};
    assert(spw_port_send(a, &one, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_LINK_UNAVAILABLE);
    std::uint8_t receive_byte = 0u;
    spw_packet_t one_rx{&receive_byte, 0u, 1u, SPW_TERMINATOR_EOP};
    assert(spw_port_receive(a, &one_rx, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_LINK_UNAVAILABLE);

    spw_port_t* duplicate_a = nullptr;
    simulator = (spw_simulator_config_t)SPW_SIMULATOR_CONFIG_INITIALIZER;
    simulator.link_id = link_id;
    simulator.endpoint = SPW_SIMULATOR_ENDPOINT_A;
    config = (spw_port_config_t)SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
    config.backend_config = &simulator;
    config.backend_config_size = sizeof(simulator);
    assert(spw_port_open(&config, &duplicate_a) == SPW_ERR_RESOURCE_EXHAUSTED);
    assert(duplicate_a == nullptr);

    spw_port_t* b = open_endpoint(link_id, SPW_SIMULATOR_ENDPOINT_B);
    assert(state_of(b) == SPW_LINK_ERROR_RESET);
    assert(spw_port_start(b) == SPW_OK);
    assert(state_of(a) == SPW_LINK_RUN);
    assert(state_of(b) == SPW_LINK_RUN);

    spw_capabilities_t caps{};
    assert(spw_port_get_capabilities(a, &caps) == SPW_OK);
    assert((caps.bits & SPW_CAP_ZERO_COPY) != 0u);
    assert(caps.max_packet_size == 4096u);
    assert(caps.tx_queue_depth == 8u && caps.rx_queue_depth == 8u);

    /* Maximum packet succeeds; one byte above fails before touching payload. */
    std::array<std::uint8_t, 4096> max_tx{};
    std::array<std::uint8_t, 4096> max_rx{};
    for (std::size_t i = 0; i < max_tx.size(); ++i) {
        max_tx[i] = static_cast<std::uint8_t>((i * 13u + 3u) & 0xffu);
    }
    spw_packet_t max_packet{max_tx.data(), max_tx.size(), max_tx.size(), SPW_TERMINATOR_EEP};
    assert(spw_port_send(a, &max_packet, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    spw_packet_t max_received{max_rx.data(), 0u, max_rx.size(), SPW_TERMINATOR_EOP};
    assert(spw_port_receive(b, &max_received, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(max_received.length == max_tx.size());
    assert(max_received.terminator == SPW_TERMINATOR_EEP);
    assert(max_rx == max_tx);
    spw_packet_t oversize{&byte, 4097u, 4097u, SPW_TERMINATOR_EOP};
    assert(spw_port_send(a, &oversize, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_INVALID_PACKET);

    /* Finite waits terminate deterministically. */
    one_rx.length = 0u;
    assert(spw_port_receive(b, &one_rx, 1000u) == SPW_ERR_TIMEOUT);
    spw_time_code_t tc_rx{};
    assert(spw_port_receive_time_code(b, &tc_rx, 1000u) == SPW_ERR_TIMEOUT);

    /* Time-code boundaries and bounded time-code queue. */
    spw_time_code_t tc{63u, 0u};
    assert(spw_port_send_time_code(a, &tc, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(spw_port_receive_time_code(b, &tc_rx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(tc_rx.time_count == 63u && tc_rx.control_flags == 0u);
    tc = {64u, 0u};
    assert(spw_port_send_time_code(a, &tc, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_INVALID_ARGUMENT);
    tc = {1u, 1u};
    assert(spw_port_send_time_code(a, &tc, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_INVALID_ARGUMENT);
    tc = {7u, 0u};
    for (std::size_t i = 0; i < 8u; ++i) {
        assert(spw_port_send_time_code(a, &tc, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    }
    assert(spw_port_send_time_code(a, &tc, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_RESOURCE_EXHAUSTED);
    for (std::size_t i = 0; i < 8u; ++i) {
        assert(spw_port_receive_time_code(b, &tc_rx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    }

    /* Zero-copy TX pool boundaries and ownership transitions. */
    assert(spw_port_acquire_tx_buffer(a, 4097u, SPW_TIMEOUT_IMMEDIATE, nullptr) == SPW_ERR_INVALID_ARGUMENT);
    spw_buffer_t* too_large = reinterpret_cast<spw_buffer_t*>(0x1);
    assert(spw_port_acquire_tx_buffer(a, 4097u, SPW_TIMEOUT_IMMEDIATE, &too_large) == SPW_ERR_BUFFER_TOO_SMALL);
    assert(too_large == nullptr);

    std::array<spw_buffer_t*, 8> acquired{};
    for (auto& buffer : acquired) {
        assert(spw_port_acquire_tx_buffer(a, 1u, SPW_TIMEOUT_IMMEDIATE, &buffer) == SPW_OK);
        assert(buffer != nullptr);
        spw_buffer_view_t view{};
        assert(spw_buffer_get_view(buffer, &view) == SPW_OK);
        assert(view.data != nullptr && view.capacity >= 1u);
        assert((reinterpret_cast<std::uintptr_t>(view.data) % caps.buffer_alignment) == 0u);
    }
    spw_buffer_t* ninth = reinterpret_cast<spw_buffer_t*>(0x1);
    assert(spw_port_acquire_tx_buffer(a, 1u, SPW_TIMEOUT_IMMEDIATE, &ninth) == SPW_ERR_RESOURCE_EXHAUSTED);
    assert(ninth == nullptr);

    spw_buffer_t* first = acquired[0];
    spw_buffer_view_t first_view{};
    assert(spw_buffer_get_view(first, &first_view) == SPW_OK);
    first_view.data[0] = 0xabu;
    assert(spw_buffer_set_packet(first, first_view.capacity + 1u, SPW_TERMINATOR_EOP) == SPW_ERR_INVALID_PACKET);
    assert(spw_buffer_set_packet(first, 1u, static_cast<spw_terminator_t>(9u)) == SPW_ERR_INVALID_PACKET);
    assert(spw_buffer_set_packet(first, 1u, SPW_TERMINATOR_EEP) == SPW_OK);

    /* Foreign-port operations reject ownership and preserve the pointer. */
    spw_buffer_t* foreign = first;
    assert(spw_port_submit_tx_buffer(b, &foreign, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_INVALID_STATE);
    assert(foreign == first);
    assert(spw_port_release_tx_buffer(b, &foreign) == SPW_ERR_INVALID_STATE);
    assert(foreign == first);

    assert(spw_port_submit_tx_buffer(a, &first, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(first == nullptr);
    acquired[0] = nullptr;

    spw_buffer_t* reclaimed = nullptr;
    assert(spw_port_reclaim_tx_buffer(a, SPW_TIMEOUT_IMMEDIATE, &reclaimed) == SPW_OK);
    assert(reclaimed != nullptr);
    spw_buffer_view_t reclaimed_view{};
    assert(spw_buffer_get_view(reclaimed, &reclaimed_view) == SPW_OK);
    assert(reclaimed_view.length == 1u && reclaimed_view.terminator == SPW_TERMINATOR_EEP);
    assert(spw_port_reclaim_tx_buffer(a, SPW_TIMEOUT_IMMEDIATE, &first) == SPW_ERR_TIMEOUT);
    assert(first == nullptr);
    assert(spw_port_release_tx_buffer(a, &reclaimed) == SPW_OK);
    assert(reclaimed == nullptr);

    for (auto& buffer : acquired) {
        if (buffer != nullptr) {
            assert(spw_port_release_tx_buffer(a, &buffer) == SPW_OK);
            assert(buffer == nullptr);
        }
    }

    /* Submitted zero-copy packet is visible through the copied RX path. */
    assert(spw_port_receive(b, &one_rx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(receive_byte == 0xabu && one_rx.length == 1u &&
           one_rx.terminator == SPW_TERMINATOR_EEP);

    /* Copied TX is visible through zero-copy RX and RX metadata is immutable. */
    byte = 0x5cu;
    one = {&byte, 1u, 1u, SPW_TERMINATOR_EOP};
    assert(spw_port_send(a, &one, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    spw_buffer_t* rx_buffer = nullptr;
    assert(spw_port_acquire_rx_buffer(b, SPW_TIMEOUT_IMMEDIATE, &rx_buffer) == SPW_OK);
    assert(rx_buffer != nullptr);
    spw_buffer_view_t rx_view{};
    assert(spw_buffer_get_view(rx_buffer, &rx_view) == SPW_OK);
    assert(rx_view.length == 1u && rx_view.data[0] == byte &&
           rx_view.terminator == SPW_TERMINATOR_EOP);
    assert(spw_buffer_set_packet(rx_buffer, 0u, SPW_TERMINATOR_EOP) == SPW_ERR_INVALID_STATE);
    spw_buffer_t* foreign_rx = rx_buffer;
    assert(spw_port_release_rx_buffer(a, &foreign_rx) == SPW_ERR_INVALID_STATE);
    assert(foreign_rx == rx_buffer);
    assert(spw_port_release_rx_buffer(b, &rx_buffer) == SPW_OK);
    assert(rx_buffer == nullptr);
    assert(spw_port_release_rx_buffer(b, &rx_buffer) == SPW_ERR_INVALID_ARGUMENT);
    assert(spw_port_acquire_rx_buffer(b, SPW_TIMEOUT_IMMEDIATE, &rx_buffer) == SPW_ERR_TIMEOUT);
    assert(rx_buffer == nullptr);

    /* Stop/reset/disconnect and recovery keep the surviving handle valid. */
    assert(spw_port_stop(b) == SPW_OK);
    assert(spw_port_stop(b) == SPW_OK);
    assert(state_of(b) == SPW_LINK_READY);
    assert(state_of(a) == SPW_LINK_CONNECTING);
    assert(spw_port_send(a, &one, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_LINK_UNAVAILABLE);
    assert(spw_port_start(b) == SPW_OK);
    assert(state_of(a) == SPW_LINK_RUN && state_of(b) == SPW_LINK_RUN);

    assert(spw_port_reset(b) == SPW_OK);
    assert(spw_port_reset(b) == SPW_OK);
    assert(state_of(b) == SPW_LINK_ERROR_RESET);
    assert(state_of(a) == SPW_LINK_CONNECTING);
    assert(spw_port_start(b) == SPW_OK);
    assert(state_of(a) == SPW_LINK_RUN && state_of(b) == SPW_LINK_RUN);

    assert(spw_port_close(b) == SPW_OK);
    b = nullptr;
    assert(state_of(a) == SPW_LINK_CONNECTING);
    assert(spw_port_send(a, &one, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_LINK_UNAVAILABLE);
    b = open_endpoint(link_id, SPW_SIMULATOR_ENDPOINT_B);
    assert(spw_port_start(b) == SPW_OK);
    assert(state_of(a) == SPW_LINK_RUN && state_of(b) == SPW_LINK_RUN);

    assert(spw_port_close(b) == SPW_OK);
    assert(spw_port_close(a) == SPW_OK);

    /* Registry resource boundary: 16 distinct links, then deterministic failure. */
    std::array<spw_port_t*, 16> links{};
    for (std::size_t i = 0; i < links.size(); ++i) {
        links[i] = open_endpoint(0x8000u + i, SPW_SIMULATOR_ENDPOINT_A);
    }
    simulator = (spw_simulator_config_t)SPW_SIMULATOR_CONFIG_INITIALIZER;
    simulator.link_id = 0x9000u;
    simulator.endpoint = SPW_SIMULATOR_ENDPOINT_A;
    config = (spw_port_config_t)SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
    config.backend_config = &simulator;
    config.backend_config_size = sizeof(simulator);
    spw_port_t* seventeenth = nullptr;
    assert(spw_port_open(&config, &seventeenth) == SPW_ERR_RESOURCE_EXHAUSTED);
    assert(seventeenth == nullptr);
    for (auto* link : links) {
        assert(spw_port_close(link) == SPW_OK);
    }

    return 0;
}
