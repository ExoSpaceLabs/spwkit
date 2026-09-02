// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.hpp>

#include <array>
#include <cstdint>
#include <cstring>

int main() {
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);

    spwkit::WorkspaceRequirements requirements{};
    if (spwkit::Port::workspace_requirements(config, requirements) != SPW_OK ||
        requirements.size == 0u || requirements.alignment == 0u) {
        return 1;
    }

    spwkit::Port port;
    if (spwkit::Port::open(config, port) != SPW_OK || port.start() != SPW_OK) {
        return 2;
    }

    std::array<std::uint8_t, 4> tx{{0x53u, 0x50u, 0x57u, 0x4bu}};
    if (port.send(tx.data(), tx.size(), SPW_TERMINATOR_EEP) != SPW_OK) {
        return 3;
    }

    std::array<std::uint8_t, 4> rx{};
    spw_packet_t packet{rx.data(), 0u, rx.size(), SPW_TERMINATOR_EOP};
    if (port.receive(packet) != SPW_OK) {
        return 4;
    }

    if (packet.length != tx.size() || packet.terminator != SPW_TERMINATOR_EEP ||
        std::memcmp(tx.data(), rx.data(), tx.size()) != 0) {
        return 5;
    }

    // Loopback deliberately does not advertise zero-copy. Exercise the C++
    // forwarding surface and require the same capability-gated result as C.
    spwkit::Buffer* unsupported = nullptr;
    if (port.acquire_tx_buffer(1u, unsupported) != SPW_ERR_UNSUPPORTED ||
        unsupported != nullptr) {
        return 6;
    }

    return port.stop() == SPW_OK ? 0 : 7;
}
