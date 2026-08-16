// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L

#include <spwkit/device.h>
#include <spwkit/spwkit.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace {
constexpr spwkit::Timeout timeout_us = UINT64_C(3000000);

bool wait_run(spwkit::Port& port) {
    for (unsigned attempt = 0; attempt < 150; ++attempt) {
        spw_link_state_t state = SPW_LINK_ERROR_RESET;
        if (port.link_state(state) == SPW_OK && state == SPW_LINK_RUN) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

bool wait_ready(spwkit::Port& port, spw_ready_events_t interest) {
    spw_ready_events_t ready = SPW_READY_NONE;
    return port.wait(interest, ready, timeout_us) == SPW_OK &&
           (ready & interest) != 0u;
}

bool receive_bytes(spwkit::Port& port,
                   const std::uint8_t* expected,
                   std::size_t expected_length,
                   spw_terminator_t terminator) {
    std::array<std::uint8_t, 32> storage{};
    spw_packet_t packet{storage.data(), 0u, storage.size(), SPW_TERMINATOR_EOP};
    return wait_ready(port, SPW_READY_RX_PACKET) &&
           port.receive(packet, timeout_us) == SPW_OK &&
           packet.length == expected_length && packet.terminator == terminator &&
           std::memcmp(packet.data, expected, expected_length) == 0;
}
}  // namespace

int main(int argc, char** argv) {
    std::array<std::uint8_t, 5> hello{'h', 'e', 'l', 'l', 'o'};
    std::array<std::uint8_t, 5> world{'w', 'o', 'r', 'l', 'd'};

    if (argc != 3) {
        std::fprintf(stderr, "usage: %s SOCKET 0|1\n", argv[0]);
        return 2;
    }

    char* end = nullptr;
    const unsigned long port_id = std::strtoul(argv[2], &end, 10);
    if (end == argv[2] || *end != '\0' || port_id > 1u) {
        return 2;
    }

    spw_device_config_t device = SPW_DEVICE_CONFIG_INITIALIZER(
        static_cast<std::uint32_t>(port_id));
    const std::size_t endpoint_length = std::strlen(argv[1]);
    if (endpoint_length >= sizeof(device.endpoint)) {
        return 2;
    }
    std::memcpy(device.endpoint, argv[1], endpoint_length + 1u);

    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_DEVICE);
    config.backend_config = &device;
    config.backend_config_size = sizeof(device);

    spwkit::Port port;
    if (spwkit::Port::open(config, port) != SPW_OK ||
        port.start() != SPW_OK || !wait_run(port)) {
        return 1;
    }

    if (port_id == 0u) {
        spw_time_code_t time_code{7u, 0u};
        if (port.send(hello.data(), hello.size(), SPW_TERMINATOR_EOP, timeout_us) != SPW_OK ||
            port.send_time_code(time_code, timeout_us) != SPW_OK ||
            !receive_bytes(port, world.data(), world.size(), SPW_TERMINATOR_EEP)) {
            return 1;
        }
    } else {
        spw_time_code_t time_code{};
        if (!receive_bytes(port, hello.data(), hello.size(), SPW_TERMINATOR_EOP) ||
            !wait_ready(port, SPW_READY_RX_TIME_CODE) ||
            port.receive_time_code(time_code, timeout_us) != SPW_OK ||
            time_code.time_count != 7u || time_code.control_flags != 0u ||
            port.send(world.data(), world.size(), SPW_TERMINATOR_EEP, timeout_us) != SPW_OK) {
            return 1;
        }
    }

    std::printf("installed C++ device peer %lu completed\n", port_id);
    return 0;
}
