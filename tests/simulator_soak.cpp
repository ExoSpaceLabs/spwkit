// SPDX-License-Identifier: Apache-2.0
#include <spwkit/spwkit.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

unsigned iterations() {
    const char* value = std::getenv("SPWKIT_SOAK_ITERATIONS");
    if (value == nullptr || *value == '\0') {
        return 100u;
    }
    const unsigned long parsed = std::strtoul(value, nullptr, 10);
    assert(parsed >= 1u && parsed <= 100000u);
    return static_cast<unsigned>(parsed);
}

spw_port_t* open_endpoint(std::uint64_t link_id, spw_simulator_endpoint_t endpoint) {
    spw_simulator_config_t simulator = SPW_SIMULATOR_CONFIG_INITIALIZER;
    simulator.link_id = link_id;
    simulator.endpoint = endpoint;
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
    config.backend_config = &simulator;
    config.backend_config_size = sizeof(simulator);
    spw_port_t* port = nullptr;
    assert(spw_port_open(&config, &port) == SPW_OK);
    assert(spw_port_start(port) == SPW_OK);
    return port;
}

void copied_round_trip(spw_port_t* a, spw_port_t* b, std::uint8_t seed) {
    std::array<std::uint8_t, 64> tx{};
    for (std::size_t i = 0; i < tx.size(); ++i) {
        tx[i] = static_cast<std::uint8_t>(seed + i);
    }
    spw_packet_t out{tx.data(), tx.size(), tx.size(), SPW_TERMINATOR_EOP};
    assert(spw_port_send(a, &out, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);

    std::array<std::uint8_t, 64> rx{};
    spw_packet_t in{rx.data(), 0u, rx.size(), SPW_TERMINATOR_EEP};
    assert(spw_port_receive(b, &in, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(in.length == tx.size());
    assert(in.terminator == SPW_TERMINATOR_EOP);
    assert(std::memcmp(tx.data(), rx.data(), tx.size()) == 0);
}

void zero_copy_round_trip(spw_port_t* a, spw_port_t* b, std::uint8_t seed) {
    spw_buffer_t* tx = nullptr;
    assert(spw_port_acquire_tx_buffer(a, 32u, SPW_TIMEOUT_IMMEDIATE, &tx) == SPW_OK);
    assert(tx != nullptr);

    spw_buffer_view_t tx_view{};
    assert(spw_buffer_get_view(tx, &tx_view) == SPW_OK);
    assert(tx_view.capacity >= 32u);
    for (std::size_t i = 0; i < 32u; ++i) {
        tx_view.data[i] = static_cast<std::uint8_t>(seed ^ i);
    }
    assert(spw_buffer_set_packet(tx, 32u, SPW_TERMINATOR_EEP) == SPW_OK);
    assert(spw_port_submit_tx_buffer(a, &tx, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(tx == nullptr);

    spw_buffer_t* rx = nullptr;
    assert(spw_port_acquire_rx_buffer(b, SPW_TIMEOUT_IMMEDIATE, &rx) == SPW_OK);
    spw_buffer_view_t rx_view{};
    assert(spw_buffer_get_view(rx, &rx_view) == SPW_OK);
    assert(rx_view.length == 32u);
    assert(rx_view.terminator == SPW_TERMINATOR_EEP);
    for (std::size_t i = 0; i < 32u; ++i) {
        assert(rx_view.data[i] == static_cast<std::uint8_t>(seed ^ i));
    }
    assert(spw_port_release_rx_buffer(b, &rx) == SPW_OK);
    assert(rx == nullptr);

    assert(spw_port_reclaim_tx_buffer(a, SPW_TIMEOUT_IMMEDIATE, &tx) == SPW_OK);
    assert(tx != nullptr);
    assert(spw_port_release_tx_buffer(a, &tx) == SPW_OK);
    assert(tx == nullptr);
}

} // namespace

int main() {
    const unsigned count = iterations();

    for (unsigned i = 0u; i < count; ++i) {
        const std::uint64_t link_id = 0x534f414b00000000ull + i;
        spw_port_t* a = open_endpoint(link_id, SPW_SIMULATOR_ENDPOINT_A);
        spw_port_t* b = open_endpoint(link_id, SPW_SIMULATOR_ENDPOINT_B);

        copied_round_trip(a, b, static_cast<std::uint8_t>(i));
        zero_copy_round_trip(a, b, static_cast<std::uint8_t>(i + 17u));

        /* Exhaust and fully recover the bounded TX pool every cycle. */
        std::array<spw_buffer_t*, 8> held{};
        std::size_t acquired = 0u;
        while (acquired < held.size() &&
               spw_port_acquire_tx_buffer(a, 1u, SPW_TIMEOUT_IMMEDIATE,
                                          &held[acquired]) == SPW_OK) {
            ++acquired;
        }
        assert(acquired > 0u);
        spw_buffer_t* extra = nullptr;
        assert(spw_port_acquire_tx_buffer(a, 1u, SPW_TIMEOUT_IMMEDIATE, &extra) ==
               SPW_ERR_RESOURCE_EXHAUSTED);
        assert(extra == nullptr);
        for (std::size_t n = 0u; n < acquired; ++n) {
            assert(spw_port_release_tx_buffer(a, &held[n]) == SPW_OK);
            assert(held[n] == nullptr);
        }

        /* Reset creates a fresh ownership/lifecycle epoch, then traffic resumes. */
        assert(spw_port_reset(a) == SPW_OK);
        assert(spw_port_start(a) == SPW_OK);
        copied_round_trip(b, a, static_cast<std::uint8_t>(i + 33u));

        assert(spw_port_stop(a) == SPW_OK);
        assert(spw_port_stop(b) == SPW_OK);
        assert(spw_port_close(a) == SPW_OK);
        assert(spw_port_close(b) == SPW_OK);
    }
    return 0;
}
