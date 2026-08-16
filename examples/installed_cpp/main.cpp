// SPDX-License-Identifier: Apache-2.0
#include <spwkit/spwkit.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <utility>

int main() {
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);
    spwkit::Port port;
    if (spwkit::Port::open(config, port) != SPW_OK || !port) {
        return 1;
    }
    if (port.start() != SPW_OK) {
        return 2;
    }

    std::array<std::uint8_t, 4> tx{{1u, 2u, 3u, 4u}};
    if (port.send(tx.data(), tx.size(), SPW_TERMINATOR_EEP) != SPW_OK) {
        return 3;
    }

    std::array<std::uint8_t, 4> rx{};
    spw_packet_t packet{rx.data(), 0u, rx.size(), SPW_TERMINATOR_EOP};
    if (port.receive(packet) != SPW_OK || packet.length != tx.size() ||
        packet.terminator != SPW_TERMINATOR_EEP ||
        std::memcmp(tx.data(), rx.data(), tx.size()) != 0) {
        return 4;
    }

    /* Move-only RAII owns the same opaque C handle, not a parallel runtime. */
    spwkit::Port moved = std::move(port);
    if (port || !moved) {
        return 5;
    }
    return moved.close() == SPW_OK ? 0 : 6;
}
