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
                     const std::array<spw_udp_fault_rule_t, SPW_UDP_FAULT_RULE_COUNT>& rules) {
    spw_udp_config_t udp = SPW_UDP_CONFIG_INITIALIZER(local_port, remote_port, link_id);
    udp.fragment_payload_size = 512u;
    udp.ack_timeout_ms = 20u;
    udp.max_retries = 4u;
    udp.keepalive_interval_ms = 20u;
    udp.peer_timeout_ms = 250u;
    udp.fault_seed = 0x30f00d1234567890ull;
    for (std::size_t i = 0u; i < rules.size(); ++i) {
        udp.fault_rules[i] = rules[i];
    }

    spw_port_config_t config = SPW_PORT_CONFIG_INITIALIZER(SPW_BACKEND_UDP);
    config.backend_config = &udp;
    config.backend_config_size = sizeof(udp);
    spw_port_t* port = nullptr;
    assert(spw_port_open(&config, &port) == SPW_OK);
    assert(spw_port_start(port) == SPW_OK);
    return port;
}

std::array<spw_udp_fault_rule_t, SPW_UDP_FAULT_RULE_COUNT> no_faults() {
    std::array<spw_udp_fault_rule_t, SPW_UDP_FAULT_RULE_COUNT> rules{};
    for (auto& rule : rules) {
        rule = SPW_UDP_FAULT_RULE_INITIALIZER;
    }
    return rules;
}

spw_udp_fault_rule_t always(spw_udp_fault_action_t action,
                            spw_udp_fault_target_t target,
                            std::uint32_t max_events = 1u,
                            std::uint32_t delay_us = 0u) {
    return {action, target, SPW_UDP_FAULT_PROBABILITY_SCALE,
            max_events, delay_us, 0u};
}

void establish(spw_port_t* a, spw_port_t* b) {
    spw_link_state_t as = SPW_LINK_CONNECTING;
    spw_link_state_t bs = SPW_LINK_CONNECTING;
    for (unsigned i = 0u; i < 100u; ++i) {
        assert(spw_port_get_link_state(a, &as) == SPW_OK);
        assert(spw_port_get_link_state(b, &bs) == SPW_OK);
        if (as == SPW_LINK_RUN && bs == SPW_LINK_RUN) {
            return;
        }
        ::usleep(1000u);
    }
    assert(false && "UDP peers did not establish");
}

void close_pair(spw_port_t* a, spw_port_t* b) {
    assert(spw_port_close(a) == SPW_OK);
    assert(spw_port_close(b) == SPW_OK);
}

} // namespace

int main() {
    const std::uint16_t base = static_cast<std::uint16_t>(48000u +
        (static_cast<unsigned>(::getpid()) % 300u) * 6u);

    /* Dropped ACK: reliable resend occurs, but the logical packet is delivered once. */
    {
        auto ar = no_faults();
        auto br = no_faults();
        br[0] = always(SPW_UDP_FAULT_ACTION_TRANSPORT_DROP, SPW_UDP_FAULT_TARGET_ACK);
        spw_port_t* a = open_udp(base, base + 1u, 0x3001u, ar);
        spw_port_t* b = open_udp(base + 1u, base, 0x3001u, br);
        establish(a, b);

        std::array<std::uint8_t, 4> bytes{{1u, 2u, 3u, 4u}};
        spw_packet_t out{bytes.data(), bytes.size(), bytes.size(), SPW_TERMINATOR_EOP};
        assert(spw_port_send(a, &out, 200000u) == SPW_OK);
        std::array<std::uint8_t, 4> rx{};
        spw_packet_t in{rx.data(), 0u, rx.size(), SPW_TERMINATOR_EOP};
        assert(spw_port_receive(b, &in, 200000u) == SPW_OK);
        assert(std::memcmp(bytes.data(), rx.data(), bytes.size()) == 0);

        ::usleep(30000u);
        spw_link_state_t state{};
        assert(spw_port_get_link_state(a, &state) == SPW_OK);
        for (unsigned i = 0u; i < 6u; ++i) {
            spw_packet_t none{rx.data(), 0u, rx.size(), SPW_TERMINATOR_EOP};
            assert(spw_port_receive(b, &none, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_TIMEOUT);
        }
        assert(spw_port_get_link_state(a, &state) == SPW_OK);
        assert(state == SPW_LINK_RUN);

        spw_fault_statistics_t fs{};
        assert(spw_port_get_fault_statistics(b, &fs) == SPW_OK);
        assert(fs.transport_drops == 1u);
        assert(fs.spacewire_eep_injections == 0u);
        close_pair(a, b);
    }

    /* Duplicate then reorder DATA. #28 reassembly makes both harmless to the API. */
    {
        auto ar = no_faults();
        auto br = no_faults();
        ar[0] = always(SPW_UDP_FAULT_ACTION_TRANSPORT_DUPLICATE,
                       SPW_UDP_FAULT_TARGET_DATA);
        ar[1] = always(SPW_UDP_FAULT_ACTION_TRANSPORT_REORDER,
                       SPW_UDP_FAULT_TARGET_DATA);
        spw_port_t* a = open_udp(base + 2u, base + 3u, 0x3002u, ar);
        spw_port_t* b = open_udp(base + 3u, base + 2u, 0x3002u, br);
        establish(a, b);

        std::array<std::uint8_t, 5> small{{9u, 8u, 7u, 6u, 5u}};
        spw_packet_t small_out{small.data(), small.size(), small.size(), SPW_TERMINATOR_EOP};
        assert(spw_port_send(a, &small_out, 200000u) == SPW_OK);
        std::array<std::uint8_t, 5> small_rx{};
        spw_packet_t small_in{small_rx.data(), 0u, small_rx.size(), SPW_TERMINATOR_EOP};
        assert(spw_port_receive(b, &small_in, 200000u) == SPW_OK);
        for (unsigned i = 0u; i < 4u; ++i) {
            spw_packet_t none{small_rx.data(), 0u, small_rx.size(), SPW_TERMINATOR_EOP};
            assert(spw_port_receive(b, &none, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_TIMEOUT);
        }

        std::array<std::uint8_t, 1400> large{};
        for (std::size_t i = 0u; i < large.size(); ++i) {
            large[i] = static_cast<std::uint8_t>((i * 31u + 7u) & 0xffu);
        }
        spw_packet_t large_out{large.data(), large.size(), large.size(), SPW_TERMINATOR_EEP};
        assert(spw_port_send(a, &large_out, 200000u) == SPW_OK);
        std::array<std::uint8_t, 1400> large_rx{};
        spw_packet_t large_in{large_rx.data(), 0u, large_rx.size(), SPW_TERMINATOR_EOP};
        assert(spw_port_receive(b, &large_in, 200000u) == SPW_OK);
        assert(large_in.terminator == SPW_TERMINATOR_EEP);
        assert(std::memcmp(large.data(), large_rx.data(), large.size()) == 0);

        spw_fault_statistics_t fs{};
        assert(spw_port_get_fault_statistics(a, &fs) == SPW_OK);
        assert(fs.transport_duplicates == 1u);
        assert(fs.transport_reorders == 1u);
        assert(fs.transport_drops == 0u);
        close_pair(a, b);
    }

    /* Transport delay is not EEP; explicit SpaceWire EEP injection is. */
    {
        auto ar = no_faults();
        auto br = no_faults();
        ar[0] = always(SPW_UDP_FAULT_ACTION_TRANSPORT_DELAY,
                       SPW_UDP_FAULT_TARGET_TIME_CODE, 1u, 2000u);
        ar[1] = always(SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP,
                       SPW_UDP_FAULT_TARGET_DATA);
        spw_port_t* a = open_udp(base + 4u, base + 5u, 0x3003u, ar);
        spw_port_t* b = open_udp(base + 5u, base + 4u, 0x3003u, br);
        establish(a, b);

        const spw_time_code_t tc{42u, 0u};
        assert(spw_port_send_time_code(a, &tc, SPW_TIMEOUT_IMMEDIATE) == SPW_ERR_TIMEOUT);
        assert(spw_port_send_time_code(a, &tc, 200000u) == SPW_OK);
        spw_time_code_t got{};
        assert(spw_port_receive_time_code(b, &got, 200000u) == SPW_OK);
        assert(got.time_count == tc.time_count);

        std::array<std::uint8_t, 3> bytes{{4u, 5u, 6u}};
        spw_packet_t out{bytes.data(), bytes.size(), bytes.size(), SPW_TERMINATOR_EOP};
        assert(spw_port_send(a, &out, 200000u) == SPW_OK);
        std::array<std::uint8_t, 3> rx{};
        spw_packet_t in{rx.data(), 0u, rx.size(), SPW_TERMINATOR_EOP};
        assert(spw_port_receive(b, &in, 200000u) == SPW_OK);
        assert(in.terminator == SPW_TERMINATOR_EEP);

        spw_fault_statistics_t fs{};
        assert(spw_port_get_fault_statistics(a, &fs) == SPW_OK);
        assert(fs.transport_delays == 1u);
        assert(fs.spacewire_eep_injections == 1u);
        assert(fs.transport_drops == 0u);
        assert(spw_port_clear_fault_statistics(a) == SPW_OK);
        assert(spw_port_get_fault_statistics(a, &fs) == SPW_OK);
        assert(fs.transport_delays == 0u && fs.spacewire_eep_injections == 0u);
        close_pair(a, b);
    }

    return 0;
}
