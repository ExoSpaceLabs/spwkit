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
                     std::uint32_t link_id,
                     std::uint16_t fragment_size) {
    spw_udp_config_t udp = SPW_UDP_CONFIG_INITIALIZER(local_port, remote_port, link_id);
    udp.fragment_payload_size = fragment_size;
    udp.ack_timeout_ms = 20u;
    udp.max_retries = 3u;
    udp.keepalive_interval_ms = 30u;
    udp.peer_timeout_ms = 120u;

    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
    config.backend_config = &udp;
    config.backend_config_size = sizeof(udp);

    spw_port_t* port = nullptr;
    assert(spw_port_open(&config, &port) == SPW_OK);
    assert(port != nullptr);
    assert(spw_port_start(port) == SPW_OK);
    return port;
}

} // namespace

int main() {
    const std::uint16_t base = static_cast<std::uint16_t>(43000u +
        (static_cast<unsigned>(::getpid()) % 500u) * 2u);
    constexpr std::uint32_t link_id = 0x10203040u;

    spw_port_t* a = open_udp(base, static_cast<std::uint16_t>(base + 1u), link_id, 512u);
    spw_port_t* b = open_udp(static_cast<std::uint16_t>(base + 1u), base, link_id, 512u);

    std::array<std::uint8_t, 5000> tx{};
    for (std::size_t i = 0; i < tx.size(); ++i) {
        tx[i] = static_cast<std::uint8_t>((i * 17u + 3u) & 0xffu);
    }

    spw_packet_t outgoing{tx.data(), tx.size(), tx.size(), SPW_TERMINATOR_EEP};
    assert(spw_port_send(a, &outgoing, 500000u) == SPW_OK);

    std::array<std::uint8_t, 16> tiny{};
    spw_packet_t too_small{tiny.data(), 0u, tiny.size(), SPW_TERMINATOR_EOP};
    assert(spw_port_receive(b, &too_small, 500000u) == SPW_ERR_BUFFER_TOO_SMALL);
    assert(too_small.length == tx.size());
    assert(too_small.terminator == SPW_TERMINATOR_EEP);

    std::array<std::uint8_t, 5000> rx{};
    spw_packet_t incoming{rx.data(), 0u, rx.size(), SPW_TERMINATOR_EOP};
    assert(spw_port_receive(b, &incoming, SPW_TIMEOUT_IMMEDIATE) == SPW_OK);
    assert(incoming.length == tx.size());
    assert(incoming.terminator == SPW_TERMINATOR_EEP);
    assert(std::memcmp(tx.data(), rx.data(), tx.size()) == 0);

    /*
     * Send a small packet, intentionally leave it unacknowledged long enough
     * for A to retransmit, then verify B surfaces the logical packet once.
     */
    const std::array<std::uint8_t, 5> retry_payload{{9u, 8u, 7u, 6u, 5u}};
    spw_packet_t retry_out{const_cast<std::uint8_t*>(retry_payload.data()),
                           retry_payload.size(), retry_payload.size(),
                           SPW_TERMINATOR_EOP};
    assert(spw_port_send(a, &retry_out, 500000u) == SPW_OK);
    ::usleep(30000u);

    spw_link_state_t a_state = SPW_LINK_ERROR_RESET;
    assert(spw_port_get_link_state(a, &a_state) == SPW_OK);
    assert(a_state == SPW_LINK_RUN);

    std::array<std::uint8_t, 5> retry_rx{};
    spw_packet_t retry_in{retry_rx.data(), 0u, retry_rx.size(), SPW_TERMINATOR_EOP};
    assert(spw_port_receive(b, &retry_in, 500000u) == SPW_OK);
    assert(retry_in.length == retry_payload.size());
    assert(std::memcmp(retry_payload.data(), retry_rx.data(), retry_payload.size()) == 0);

    /*
     * A keepalive may sit between the original DATA and its retransmission.
     * Service several immediate receives so the retransmitted DATA itself is
     * consumed by the transport layer. None may surface as a second packet.
     */
    for (unsigned attempt = 0u; attempt < 4u; ++attempt) {
        std::array<std::uint8_t, 5> duplicate_rx{};
        spw_packet_t duplicate_in{duplicate_rx.data(), 0u, duplicate_rx.size(),
                                  SPW_TERMINATOR_EOP};
        assert(spw_port_receive(b, &duplicate_in, SPW_TIMEOUT_IMMEDIATE) ==
               SPW_ERR_TIMEOUT);
    }

    const std::array<std::uint8_t, 5> reply{{1u, 2u, 3u, 4u, 5u}};
    spw_packet_t reply_packet{const_cast<std::uint8_t*>(reply.data()), reply.size(),
                              reply.size(), SPW_TERMINATOR_EOP};
    assert(spw_port_send(b, &reply_packet, 500000u) == SPW_OK);
    std::array<std::uint8_t, 5> reply_rx{};
    spw_packet_t reply_in{reply_rx.data(), 0u, reply_rx.size(), SPW_TERMINATOR_EOP};
    assert(spw_port_receive(a, &reply_in, 500000u) == SPW_OK);
    assert(reply_in.length == reply.size());
    assert(std::memcmp(reply.data(), reply_rx.data(), reply.size()) == 0);

    const spw_time_code_t sent_time{37u, 0u};
    assert(spw_port_send_time_code(a, &sent_time, 500000u) == SPW_OK);
    spw_time_code_t received_time{};
    assert(spw_port_receive_time_code(b, &received_time, 500000u) == SPW_OK);
    assert(received_time.time_count == sent_time.time_count);
    assert(received_time.control_flags == sent_time.control_flags);

    spw_statistics_t a_stats{};
    spw_statistics_t b_stats{};
    assert(spw_port_get_statistics(a, &a_stats) == SPW_OK);
    assert(spw_port_get_statistics(b, &b_stats) == SPW_OK);
    assert(a_stats.tx_packets == 2u);
    assert(b_stats.rx_packets == 2u);
    assert(a_stats.tx_time_codes == 1u);
    assert(b_stats.rx_time_codes == 1u);

    /*
     * Peer loss becomes visible through the public link state. Valid control
     * datagrams may already be queued when B closes, so each poll may drain
     * one stale datagram before a full quiet peer-timeout is observed.
     */
    assert(spw_port_close(b) == SPW_OK);
    b = nullptr;
    constexpr unsigned peer_loss_poll_us = 160000u; // > configured 120 ms timeout
    for (unsigned attempt = 0u;
         attempt < 4u && a_state != SPW_LINK_ERROR_WAIT;
         ++attempt) {
        ::usleep(peer_loss_poll_us);
        assert(spw_port_get_link_state(a, &a_state) == SPW_OK);
    }
    assert(a_state == SPW_LINK_ERROR_WAIT);

    /* A restarted peer advertises a new transport session and recovers RUN. */
    b = open_udp(static_cast<std::uint16_t>(base + 1u), base, link_id, 512u);
    for (unsigned attempt = 0u; attempt < 10u && a_state != SPW_LINK_RUN; ++attempt) {
        ::usleep(10000u);
        assert(spw_port_get_link_state(a, &a_state) == SPW_OK);
    }
    assert(a_state == SPW_LINK_RUN);

    assert(spw_port_close(a) == SPW_OK);
    assert(spw_port_close(b) == SPW_OK);
    return 0;
}
