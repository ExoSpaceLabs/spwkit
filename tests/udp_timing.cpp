// SPDX-License-Identifier: Apache-2.0
#include <spwkit/port.h>
#include <spwkit/udp.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <unistd.h>

namespace {

spw_port_t* open_udp(std::uint16_t local_port,
                     std::uint16_t remote_port,
                     std::uint32_t link_id) {
    spw_udp_config_t udp = SPW_UDP_CONFIG_INITIALIZER(local_port, remote_port, link_id);
    udp.fragment_payload_size = 512u;
    udp.ack_timeout_ms = 20u;
    udp.max_retries = 3u;
    udp.keepalive_interval_ms = 20u;
    udp.peer_timeout_ms = 250u;
    udp.virtual_link_bps = 1000000u;
    udp.virtual_latency_us = 2000u;

    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
    config.backend_config = &udp;
    config.backend_config_size = sizeof(udp);

    spw_port_t* port = nullptr;
    assert(spw_port_open(&config, &port) == SPW_OK);
    assert(port != nullptr);
    assert(spw_port_start(port) == SPW_OK);
    return port;
}

void establish(spw_port_t* a, spw_port_t* b) {
    spw_link_state_t a_state = SPW_LINK_CONNECTING;
    spw_link_state_t b_state = SPW_LINK_CONNECTING;
    for (unsigned attempt = 0u; attempt < 100u; ++attempt) {
        assert(spw_port_get_link_state(a, &a_state) == SPW_OK);
        assert(spw_port_get_link_state(b, &b_state) == SPW_OK);
        if (a_state == SPW_LINK_RUN && b_state == SPW_LINK_RUN) {
            return;
        }
        ::usleep(1000u);
    }
    assert(false && "UDP peers did not establish");
}

} // namespace

int main() {
    const std::uint16_t base = static_cast<std::uint16_t>(47000u +
        (static_cast<unsigned>(::getpid()) % 500u) * 2u);
    constexpr std::uint32_t link_id = 0x29A29A29u;

    spw_port_t* a = open_udp(base, static_cast<std::uint16_t>(base + 1u), link_id);
    spw_port_t* b = open_udp(static_cast<std::uint16_t>(base + 1u), base, link_id);
    establish(a, b);

    std::array<std::uint8_t, 124> tx{};
    for (std::size_t i = 0u; i < tx.size(); ++i) {
        tx[i] = static_cast<std::uint8_t>((i * 7u + 5u) & 0xffu);
    }
    spw_packet_t outgoing{tx.data(), tx.size(), tx.size(), SPW_TERMINATOR_EOP};

    assert(spw_port_send(a, &outgoing, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_TIMEOUT);

    const spw_time_code_t time_code{17u, 1u};
    assert(spw_port_send_time_code(a, &time_code, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_TIMEOUT);

    assert(spw_port_send(a, &outgoing, 200000u) == SPW_OK);
    std::array<std::uint8_t, 124> rx{};
    spw_packet_t incoming{rx.data(), 0u, rx.size(), SPW_TERMINATOR_EOP};
    assert(spw_port_receive(b, &incoming, 200000u) == SPW_OK);
    assert(incoming.length == tx.size());
    assert(std::memcmp(tx.data(), rx.data(), tx.size()) == 0);

    assert(spw_port_send_time_code(a, &time_code, 200000u) == SPW_OK);
    spw_time_code_t received{};
    assert(spw_port_receive_time_code(b, &received, 200000u) == SPW_OK);
    assert(received.time_count == time_code.time_count);
    assert(received.control_flags == time_code.control_flags);

    assert(spw_port_close(a) == SPW_OK);
    assert(spw_port_close(b) == SPW_OK);
    return 0;
}
