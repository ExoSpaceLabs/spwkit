// SPDX-License-Identifier: Apache-2.0
#include <spwkit/port.h>
#include <spwkit/udp.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace {

unsigned iterations() {
    const char* value = std::getenv("SPWKIT_SOAK_ITERATIONS");
    if (value == nullptr || *value == '\0') return 20u;
    const unsigned long parsed = std::strtoul(value, nullptr, 10);
    assert(parsed >= 1u && parsed <= 10000u);
    return static_cast<unsigned>(parsed);
}

spw_port_t* open_udp(std::uint16_t local, std::uint16_t remote, std::uint32_t link_id) {
    spw_udp_config_t udp = SPW_UDP_CONFIG_INITIALIZER(local, remote, link_id);
    udp.fragment_payload_size = 256u;
    udp.ack_timeout_ms = 10u;
    udp.max_retries = 3u;
    udp.keepalive_interval_ms = 5u;
    udp.peer_timeout_ms = 40u;

    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
    config.backend_config = &udp;
    config.backend_config_size = sizeof(udp);
    spw_port_t* port = nullptr;
    assert(spw_port_open(&config, &port) == SPW_OK);
    assert(spw_port_start(port) == SPW_OK);
    return port;
}

void wait_for(spw_port_t* port, spw_link_state_t expected, unsigned polls, useconds_t delay) {
    spw_link_state_t state = SPW_LINK_ERROR_RESET;
    for (unsigned i = 0u; i < polls; ++i) {
        assert(spw_port_get_link_state(port, &state) == SPW_OK);
        if (state == expected) return;
        ::usleep(delay);
    }
    assert(state == expected);
}

void send_receive(spw_port_t* from, spw_port_t* to, std::uint8_t seed) {
    std::array<std::uint8_t, 600> tx{};
    for (std::size_t i = 0u; i < tx.size(); ++i) {
        tx[i] = static_cast<std::uint8_t>(seed + i * 13u);
    }
    spw_packet_t out{tx.data(), tx.size(), tx.size(), SPW_TERMINATOR_EOP};
    assert(spw_port_send(from, &out, 200000u) == SPW_OK);

    std::array<std::uint8_t, 600> rx{};
    spw_packet_t in{rx.data(), 0u, rx.size(), SPW_TERMINATOR_EEP};
    assert(spw_port_receive(to, &in, 200000u) == SPW_OK);
    assert(in.length == tx.size());
    assert(in.terminator == SPW_TERMINATOR_EOP);
    assert(std::memcmp(tx.data(), rx.data(), tx.size()) == 0);
}

} // namespace

int main() {
    const std::uint16_t base = static_cast<std::uint16_t>(
        52000u + (static_cast<unsigned>(::getpid()) % 500u) * 2u);
    constexpr std::uint32_t link_id = 0x534f414bu;

    spw_port_t* a = open_udp(base, static_cast<std::uint16_t>(base + 1u), link_id);
    spw_port_t* b = open_udp(static_cast<std::uint16_t>(base + 1u), base, link_id);
    wait_for(a, SPW_LINK_RUN, 100u, 1000u);
    wait_for(b, SPW_LINK_RUN, 100u, 1000u);

    const unsigned count = iterations();
    for (unsigned i = 0u; i < count; ++i) {
        send_receive(a, b, static_cast<std::uint8_t>(i));
        send_receive(b, a, static_cast<std::uint8_t>(i + 73u));

        assert(spw_port_close(b) == SPW_OK);
        b = nullptr;
        wait_for(a, SPW_LINK_ERROR_WAIT, 20u, 10000u);

        b = open_udp(static_cast<std::uint16_t>(base + 1u), base, link_id);
        wait_for(a, SPW_LINK_RUN, 100u, 2000u);
        wait_for(b, SPW_LINK_RUN, 100u, 2000u);

        /* Traffic after every new peer session proves rollover did not leave stale state. */
        send_receive(a, b, static_cast<std::uint8_t>(i + 151u));
    }

    assert(spw_port_close(b) == SPW_OK);
    assert(spw_port_close(a) == SPW_OK);
    return 0;
}
