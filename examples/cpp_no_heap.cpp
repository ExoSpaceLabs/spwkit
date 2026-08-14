// SPDX-License-Identifier: Apache-2.0

#include <spwkit/spwkit.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>

namespace {

bool ok(const char* operation, spw_result_t result) {
    if (result == SPW_OK) {
        return true;
    }
    std::cerr << operation << " failed: " << result << '\n';
    return false;
}

} // namespace

int main() {
    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_LOOPBACK);

    spw_port_workspace_requirements_t requirements{};
    if (!ok("workspace requirements",
            spw_port_workspace_requirements(&config, &requirements))) {
        return 1;
    }

    alignas(std::max_align_t) std::array<std::byte, 65536> workspace{};
    if (requirements.size > workspace.size() ||
        requirements.alignment > alignof(std::max_align_t)) {
        std::cerr << "example workspace is too small/aligned for this build\n";
        return 1;
    }

    spw_port_t* port = nullptr;
    if (!ok("open in place",
            spw_port_open_in_place(&config,
                                   workspace.data(),
                                   workspace.size(),
                                   &port)) ||
        port == nullptr) {
        return 1;
    }

    if (!ok("start", spw_port_start(port))) {
        (void)spw_port_close(port);
        return 1;
    }

    std::array<std::uint8_t, 5> tx{{1u, 2u, 3u, 4u, 5u}};
    spw_packet_t packet{tx.data(), tx.size(), tx.size(), SPW_TERMINATOR_EOP};
    if (!ok("send", spw_port_send(port, &packet, SPW_TIMEOUT_IMMEDIATE))) {
        (void)spw_port_close(port);
        return 1;
    }

    std::array<std::uint8_t, 5> rx{};
    spw_packet_t received{rx.data(), 0u, rx.size(), SPW_TERMINATOR_EEP};
    if (!ok("receive",
            spw_port_receive(port, &received, SPW_TIMEOUT_IMMEDIATE)) ||
        received.length != tx.size() ||
        received.terminator != SPW_TERMINATOR_EOP ||
        std::memcmp(tx.data(), rx.data(), tx.size()) != 0) {
        (void)spw_port_close(port);
        return 1;
    }

    if (!ok("close", spw_port_close(port))) {
        return 1;
    }

    // The caller owns workspace and may reuse it immediately after close.
    port = nullptr;
    if (!ok("reopen in place",
            spw_port_open_in_place(&config,
                                   workspace.data(),
                                   workspace.size(),
                                   &port)) ||
        port == nullptr) {
        return 1;
    }

    return ok("final close", spw_port_close(port)) ? 0 : 1;
}
