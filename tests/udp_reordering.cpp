// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/vspw_tp.hpp"

#include <spwkit/port.h>
#include <spwkit/udp.h>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

using namespace spwkit::ethernet::vspw_tp;

int open_raw_peer(std::uint16_t local_port) {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    assert(fd >= 0);

    int reuse = 1;
    assert(::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == 0);

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(local_port);
    assert(::inet_pton(AF_INET, "127.0.0.1", &local.sin_addr) == 1);
    assert(::bind(fd, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) == 0);
    return fd;
}

spw_port_t* open_udp_receiver(std::uint16_t local_port,
                              std::uint16_t remote_port,
                              std::uint32_t link_id) {
    spw_udp_config_t udp = SPW_UDP_CONFIG_INITIALIZER(local_port, remote_port, link_id);
    udp.fragment_payload_size = 400u;
    udp.ack_timeout_ms = 20u;
    udp.max_retries = 3u;
    udp.keepalive_interval_ms = 30u;
    udp.peer_timeout_ms = 150u;

    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
    config.backend_config = &udp;
    config.backend_config_size = sizeof(udp);

    spw_port_t* port = nullptr;
    assert(spw_port_open(&config, &port) == SPW_OK);
    assert(port != nullptr);
    assert(spw_port_start(port) == SPW_OK);
    return port;
}

void send_frame(int fd,
                std::uint16_t remote_port,
                const Header& header,
                const std::uint8_t* payload) {
    std::array<std::uint8_t, 2048> datagram{};
    assert(kHeaderSize + header.payload_size <= datagram.size());
    assert(encode_header(header, datagram.data(), datagram.size()));
    if (header.payload_size != 0u) {
        assert(payload != nullptr);
        std::memcpy(datagram.data() + kHeaderSize, payload, header.payload_size);
    }

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(remote_port);
    assert(::inet_pton(AF_INET, "127.0.0.1", &remote.sin_addr) == 1);

    const std::size_t size = kHeaderSize + header.payload_size;
    const ssize_t sent = ::sendto(fd, datagram.data(), size, 0,
                                  reinterpret_cast<const sockaddr*>(&remote),
                                  sizeof(remote));
    assert(sent == static_cast<ssize_t>(size));
}

Header data_fragment(std::uint32_t link_id,
                     std::uint64_t session_id,
                     std::uint32_t sequence,
                     std::uint32_t message_id,
                     std::uint32_t offset,
                     std::uint16_t payload_size,
                     std::uint32_t total_size,
                     std::uint8_t extra_flags) {
    Header header{};
    header.type = MessageType::Data;
    header.flags = static_cast<std::uint8_t>(FlagEep | FlagAckRequired | extra_flags);
    header.payload_size = payload_size;
    header.link_id = link_id;
    header.session_id = session_id;
    header.sequence = sequence;
    header.message_id = message_id;
    header.fragment_offset = offset;
    header.total_size = total_size;
    return header;
}

} // namespace

int main() {
    const std::uint16_t base = static_cast<std::uint16_t>(45000u +
        (static_cast<unsigned>(::getpid()) % 500u) * 2u);
    const std::uint16_t raw_port = base;
    const std::uint16_t receiver_port = static_cast<std::uint16_t>(base + 1u);
    constexpr std::uint32_t link_id = 0x55667788u;
    constexpr std::uint64_t session_id = 0x123456789abcdef0ull;
    constexpr std::uint32_t message_id = 77u;

    const int raw = open_raw_peer(raw_port);
    spw_port_t* receiver = open_udp_receiver(receiver_port, raw_port, link_id);

    Header keepalive{};
    keepalive.type = MessageType::Keepalive;
    keepalive.link_id = link_id;
    keepalive.session_id = session_id;
    keepalive.sequence = 1u;
    send_frame(raw, receiver_port, keepalive, nullptr);

    std::array<std::uint8_t, 1400> payload{};
    for (std::size_t i = 0u; i < payload.size(); ++i) {
        payload[i] = static_cast<std::uint8_t>((i * 29u + 11u) & 0xffu);
    }

    const Header middle_late = data_fragment(link_id, session_id, 2u, message_id,
                                             800u, 400u, payload.size(), FlagNone);
    const Header end = data_fragment(link_id, session_id, 3u, message_id,
                                     1200u, 200u, payload.size(), FlagFragmentEnd);
    const Header start = data_fragment(link_id, session_id, 4u, message_id,
                                       0u, 400u, payload.size(), FlagFragmentStart);
    const Header middle_early = data_fragment(link_id, session_id, 5u, message_id,
                                              400u, 400u, payload.size(), FlagNone);

    /* Deliberately send out of order, including one exact duplicate fragment. */
    send_frame(raw, receiver_port, middle_late, payload.data() + 800u);
    send_frame(raw, receiver_port, middle_late, payload.data() + 800u);
    send_frame(raw, receiver_port, end, payload.data() + 1200u);
    send_frame(raw, receiver_port, start, payload.data());
    send_frame(raw, receiver_port, middle_early, payload.data() + 400u);

    std::array<std::uint8_t, 1400> received{};
    spw_packet_t packet{received.data(), 0u, received.size(), SPW_TERMINATOR_EOP};
    assert(spw_port_receive(receiver, &packet, 500000u) == SPW_OK);
    assert(packet.length == payload.size());
    assert(packet.terminator == SPW_TERMINATOR_EEP);
    assert(std::memcmp(received.data(), payload.data(), payload.size()) == 0);

    /* The duplicate transport fragment must not surface a duplicate packet. */
    spw_packet_t none{received.data(), 0u, received.size(), SPW_TERMINATOR_EOP};
    assert(spw_port_receive(receiver, &none, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_TIMEOUT);

    /*
     * Start an incomplete logical packet, keep the peer session alive, and let
     * the DATA-specific inactivity horizon expire. A different logical packet
     * can then replace the stale partial state without reopening the port.
     */
    const Header stale_start = data_fragment(
        link_id, session_id, 6u, 88u, 0u, 400u, 800u, FlagFragmentStart);
    send_frame(raw, receiver_port, stale_start, payload.data());
    assert(spw_port_receive(receiver, &none, 10000u) == SPW_ERR_TIMEOUT);

    ::usleep(100000u);
    keepalive.sequence = 7u;
    send_frame(raw, receiver_port, keepalive, nullptr);
    spw_link_state_t state = SPW_LINK_ERROR_RESET;
    assert(spw_port_get_link_state(receiver, &state) == SPW_OK);
    assert(state == SPW_LINK_RUN);

    ::usleep(70000u); /* >150 ms since the last DATA fragment, peer still current. */
    const Header replacement_end = data_fragment(
        link_id, session_id, 8u, 89u, 400u, 400u, 800u, FlagFragmentEnd);
    const Header replacement_start = data_fragment(
        link_id, session_id, 9u, 89u, 0u, 400u, 800u, FlagFragmentStart);
    send_frame(raw, receiver_port, replacement_end, payload.data() + 400u);
    send_frame(raw, receiver_port, replacement_start, payload.data());

    packet.length = 0u;
    packet.terminator = SPW_TERMINATOR_EOP;
    assert(spw_port_receive(receiver, &packet, 500000u) == SPW_OK);
    assert(packet.length == 800u);
    assert(packet.terminator == SPW_TERMINATOR_EEP);
    assert(std::memcmp(received.data(), payload.data(), 800u) == 0);

    assert(spw_port_close(receiver) == SPW_OK);
    assert(::close(raw) == 0);
    return 0;
}
