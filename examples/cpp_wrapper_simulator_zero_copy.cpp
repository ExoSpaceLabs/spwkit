// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.hpp>

#include <array>
#include <cstdint>
#include <cstring>

static spwkit::Result open_endpoint(std::uint64_t link_id,
                                    spw_simulator_endpoint_t endpoint,
                                    spwkit::Port& out) {
    spw_simulator_config_t simulator = SPW_SIMULATOR_CONFIG_INITIALIZER;
    simulator.link_id = link_id;
    simulator.endpoint = endpoint;

    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_SIMULATOR);
    config.backend_config = &simulator;
    config.backend_config_size = sizeof(simulator);
    return spwkit::Port::open(config, out);
}

int main() {
    constexpr std::uint64_t link_id = UINT64_C(0x6370707a65726f63);
    spwkit::Port a;
    spwkit::Port b;

    if (open_endpoint(link_id, SPW_SIMULATOR_ENDPOINT_A, a) != SPW_OK ||
        open_endpoint(link_id, SPW_SIMULATOR_ENDPOINT_B, b) != SPW_OK ||
        a.start() != SPW_OK || b.start() != SPW_OK) {
        return 1;
    }

    spw_capabilities_t capabilities{};
    if (a.capabilities(capabilities) != SPW_OK ||
        (capabilities.bits & SPW_CAP_ZERO_COPY) == 0u) {
        return 2;
    }

    constexpr std::array<std::uint8_t, 5> payload{{0x53u, 0x70u, 0x57u, 0x4bu, 0x21u}};
    spwkit::Buffer* tx = nullptr;
    if (a.acquire_tx_buffer(payload.size(), tx) != SPW_OK || tx == nullptr) {
        return 3;
    }

    spwkit::BufferView tx_view{};
    if (spwkit::Port::buffer_view(*tx, tx_view) != SPW_OK ||
        tx_view.capacity < payload.size()) {
        (void)a.release_tx_buffer(tx);
        return 4;
    }
    std::memcpy(tx_view.data, payload.data(), payload.size());

    if (spwkit::Port::set_packet(*tx, payload.size(), SPW_TERMINATOR_EEP) != SPW_OK ||
        a.submit_tx_buffer(tx) != SPW_OK || tx != nullptr) {
        return 5;
    }

    spwkit::Buffer* rx = nullptr;
    if (b.acquire_rx_buffer(rx) != SPW_OK || rx == nullptr) {
        return 6;
    }

    spwkit::BufferView rx_view{};
    if (spwkit::Port::buffer_view(*rx, rx_view) != SPW_OK ||
        rx_view.length != payload.size() ||
        rx_view.terminator != SPW_TERMINATOR_EEP ||
        std::memcmp(rx_view.data, payload.data(), payload.size()) != 0) {
        (void)b.release_rx_buffer(rx);
        return 7;
    }

    if (b.release_rx_buffer(rx) != SPW_OK || rx != nullptr ||
        a.reclaim_tx_buffer(tx) != SPW_OK || tx == nullptr ||
        a.release_tx_buffer(tx) != SPW_OK || tx != nullptr) {
        return 8;
    }

    return (a.stop() == SPW_OK && b.stop() == SPW_OK) ? 0 : 9;
}
