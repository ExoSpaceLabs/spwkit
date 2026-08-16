from pathlib import Path


def replace(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if old not in text:
        raise SystemExit(f"needle not found in {path}: {old[:120]!r}")
    p.write_text(text.replace(old, new, 1))


# Internal UDP backend contract.
replace(
    "src/backends/ethernet/udp_backend.hpp",
    '#include "backends/ethernet/fragment_reassembler.hpp"\n',
    '#include "backends/ethernet/deterministic_faults.hpp"\n#include "backends/ethernet/fragment_reassembler.hpp"\n',
)
replace(
    "src/backends/ethernet/udp_backend.hpp",
    '    using FragmentReassembler = spwkit::ethernet::FragmentReassembler<max_packet_size>;\n',
    '    using FragmentReassembler = spwkit::ethernet::FragmentReassembler<max_packet_size>;\n'
    '    using FaultInjector = spwkit::ethernet::DeterministicFaultInjector;\n',
)
replace(
    "src/backends/ethernet/udp_backend.hpp",
    '    spw_result_t get_statistics(spw_statistics_t& statistics) const noexcept override;\n'
    '    spw_result_t clear_statistics() noexcept override;\n',
    '    spw_result_t get_statistics(spw_statistics_t& statistics) const noexcept override;\n'
    '    spw_result_t clear_statistics() noexcept override;\n'
    '    spw_result_t get_fault_statistics(\n'
    '        spw_fault_statistics_t& statistics) const noexcept override;\n'
    '    spw_result_t clear_fault_statistics() noexcept override;\n',
)
replace(
    "src/backends/ethernet/udp_backend.hpp",
    '    spw_result_t send_datagram(const std::uint8_t* bytes,\n'
    '                               std::size_t size,\n'
    '                               spw_timeout_us_t timeout_us) noexcept;\n',
    '    spw_result_t send_datagram(const std::uint8_t* bytes,\n'
    '                               std::size_t size,\n'
    '                               spw_timeout_us_t timeout_us) noexcept;\n'
    '    spw_result_t send_datagram_raw(const std::uint8_t* bytes,\n'
    '                                   std::size_t size,\n'
    '                                   spw_timeout_us_t timeout_us) noexcept;\n'
    '    spw_result_t wait_transport_fault_delay(std::uint32_t delay_us,\n'
    '                                            spw_timeout_us_t timeout_us) noexcept;\n',
)
replace(
    "src/backends/ethernet/udp_backend.hpp",
    '    void clear_retired_sessions() noexcept;\n'
    '    void close_socket() noexcept;\n',
    '    void clear_retired_sessions() noexcept;\n'
    '    void clear_reordered_datagram() noexcept;\n'
    '    void close_socket() noexcept;\n',
)
replace(
    "src/backends/ethernet/udp_backend.hpp",
    '    spw_udp_config_t config_{};\n'
    '    VirtualLinkTiming virtual_timing_{};\n',
    '    spw_udp_config_t config_{};\n'
    '    VirtualLinkTiming virtual_timing_{};\n'
    '    FaultInjector fault_injector_;\n',
)
replace(
    "src/backends/ethernet/udp_backend.hpp",
    '    spw_statistics_t statistics_{};\n',
    '    spw_statistics_t statistics_{};\n'
    '    spw_fault_statistics_t fault_statistics_{};\n',
)
replace(
    "src/backends/ethernet/udp_backend.hpp",
    '    std::array<std::uint8_t, 64u> control_datagram_{};\n',
    '    std::array<std::uint8_t, 64u> control_datagram_{};\n'
    '    std::array<std::uint8_t, 65507u> reordered_datagram_{};\n'
    '    std::size_t reordered_datagram_size_{0u};\n'
    '    bool reordered_datagram_valid_{false};\n',
)

# Public generic fault-statistics dispatch.
replace(
    "src/core/port.cpp",
    'spw_result_t validate_udp_config(const spw_port_config_t* config) noexcept {\n',
    '''bool valid_udp_fault_rule(const spw_udp_fault_rule_t& rule) noexcept {
    if (rule.reserved != 0u ||
        rule.probability_per_10000 > SPW_UDP_FAULT_PROBABILITY_SCALE ||
        rule.target > SPW_UDP_FAULT_TARGET_KEEPALIVE ||
        rule.action > SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP) {
        return false;
    }
    if (rule.action == SPW_UDP_FAULT_ACTION_NONE) {
        return rule.target == SPW_UDP_FAULT_TARGET_ANY &&
               rule.probability_per_10000 == 0u && rule.max_events == 0u &&
               rule.delay_us == 0u;
    }
    if (rule.probability_per_10000 == 0u) {
        return false;
    }
    if (rule.action == SPW_UDP_FAULT_ACTION_TRANSPORT_DELAY) {
        return rule.delay_us != 0u;
    }
    if (rule.delay_us != 0u) {
        return false;
    }
    if (rule.action == SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP) {
        return rule.target == SPW_UDP_FAULT_TARGET_DATA;
    }
    return true;
}

spw_result_t validate_udp_config(const spw_port_config_t* config) noexcept {
''',
)
replace(
    "src/core/port.cpp",
    '        udp->local_address[0] == \'\\0\' || udp->remote_address[0] == \'\\0\') {\n'
    '        return SPW_ERR_INVALID_ARGUMENT;\n'
    '    }\n'
    '    return SPW_OK;\n'
    '}\n',
    '        udp->local_address[0] == \'\\0\' || udp->remote_address[0] == \'\\0\') {\n'
    '        return SPW_ERR_INVALID_ARGUMENT;\n'
    '    }\n'
    '    for (size_t i = 0u; i < SPW_UDP_FAULT_RULE_COUNT; ++i) {\n'
    '        if (!valid_udp_fault_rule(udp->fault_rules[i])) {\n'
    '            return SPW_ERR_INVALID_ARGUMENT;\n'
    '        }\n'
    '    }\n'
    '    return SPW_OK;\n'
    '}\n',
)
replace(
    "src/core/port.cpp",
    'spw_result_t spw_port_clear_statistics(spw_port_t* port) {\n'
    '    return validate_port(port) == SPW_OK ? port->backend->clear_statistics()\n'
    '                                         : SPW_ERR_INVALID_ARGUMENT;\n'
    '}\n\n'
    '} // extern "C"',
    'spw_result_t spw_port_clear_statistics(spw_port_t* port) {\n'
    '    return validate_port(port) == SPW_OK ? port->backend->clear_statistics()\n'
    '                                         : SPW_ERR_INVALID_ARGUMENT;\n'
    '}\n\n'
    'spw_result_t spw_port_get_fault_statistics(\n'
    '    const spw_port_t* port, spw_fault_statistics_t* out_statistics) {\n'
    '    if (validate_port(port) != SPW_OK || out_statistics == nullptr) {\n'
    '        return SPW_ERR_INVALID_ARGUMENT;\n'
    '    }\n'
    '    return port->backend->get_fault_statistics(*out_statistics);\n'
    '}\n\n'
    'spw_result_t spw_port_clear_fault_statistics(spw_port_t* port) {\n'
    '    return validate_port(port) == SPW_OK\n'
    '        ? port->backend->clear_fault_statistics()\n'
    '        : SPW_ERR_INVALID_ARGUMENT;\n'
    '}\n\n'
    '} // extern "C"',
)

# UDP backend implementation.
replace(
    "src/backends/ethernet/udp_backend.cpp",
    '#include <cstring>\n',
    '#include <cstring>\n#include <ctime>\n',
)
replace(
    "src/backends/ethernet/udp_backend.cpp",
    'UdpBackend::UdpBackend(const spw_udp_config_t& config) noexcept\n'
    '    : config_(config),\n'
    '      virtual_timing_(config.virtual_link_bps, config.virtual_latency_us) {}\n',
    'UdpBackend::UdpBackend(const spw_udp_config_t& config) noexcept\n'
    '    : config_(config),\n'
    '      virtual_timing_(config.virtual_link_bps, config.virtual_latency_us),\n'
    '      fault_injector_(config) {}\n',
)
replace(
    "src/backends/ethernet/udp_backend.cpp",
    '        config_.peer_timeout_ms <= config_.keepalive_interval_ms ||\n'
    '        config_.reserved != 0u) {\n'
    '        return SPW_ERR_INVALID_ARGUMENT;\n'
    '    }\n\n'
    '    sockaddr_in local{};\n',
    '        config_.peer_timeout_ms <= config_.keepalive_interval_ms ||\n'
    '        config_.reserved != 0u) {\n'
    '        return SPW_ERR_INVALID_ARGUMENT;\n'
    '    }\n'
    '    for (std::size_t i = 0u; i < SPW_UDP_FAULT_RULE_COUNT; ++i) {\n'
    '        if (!FaultInjector::valid_rule(config_.fault_rules[i])) {\n'
    '            return SPW_ERR_INVALID_ARGUMENT;\n'
    '        }\n'
    '    }\n\n'
    '    sockaddr_in local{};\n',
)
replace(
    "src/backends/ethernet/udp_backend.cpp",
    'void UdpBackend::clear_retired_sessions() noexcept {\n'
    '    retired_session_head_ = 0u;\n'
    '    retired_session_count_ = 0u;\n'
    '}\n\n'
    'spw_result_t UdpBackend::start() noexcept {\n',
    'void UdpBackend::clear_retired_sessions() noexcept {\n'
    '    retired_session_head_ = 0u;\n'
    '    retired_session_count_ = 0u;\n'
    '}\n\n'
    'void UdpBackend::clear_reordered_datagram() noexcept {\n'
    '    reordered_datagram_size_ = 0u;\n'
    '    reordered_datagram_valid_ = false;\n'
    '}\n\n'
    'spw_result_t UdpBackend::start() noexcept {\n',
)
replace(
    "src/backends/ethernet/udp_backend.cpp",
    '    clear_retired_sessions();\n'
    '    pending_packet_valid_ = false;\n'
    '    time_code_head_ = 0u;\n',
    '    clear_retired_sessions();\n'
    '    clear_reordered_datagram();\n'
    '    fault_injector_.reset();\n'
    '    pending_packet_valid_ = false;\n'
    '    time_code_head_ = 0u;\n',
)
# Stop and reset each contain the same state-clear sequence. Add reorder cleanup to both.
text = Path("src/backends/ethernet/udp_backend.cpp").read_text()
text = text.replace(
    '    clear_retired_sessions();\n    pending_packet_valid_ = false;\n',
    '    clear_retired_sessions();\n    clear_reordered_datagram();\n    pending_packet_valid_ = false;\n',
)
Path("src/backends/ethernet/udp_backend.cpp").write_text(text)
# Reset the deterministic schedule on explicit link reset as well.
replace(
    "src/backends/ethernet/udp_backend.cpp",
    'spw_result_t UdpBackend::reset() noexcept {\n'
    '    if (socket_fd_ < 0) {\n'
    '        return SPW_ERR_INVALID_STATE;\n'
    '    }\n'
    '    state_ = SPW_LINK_ERROR_RESET;\n',
    'spw_result_t UdpBackend::reset() noexcept {\n'
    '    if (socket_fd_ < 0) {\n'
    '        return SPW_ERR_INVALID_STATE;\n'
    '    }\n'
    '    fault_injector_.reset();\n'
    '    state_ = SPW_LINK_ERROR_RESET;\n',
)
replace(
    "src/backends/ethernet/udp_backend.cpp",
    '    capabilities.bits = SPW_CAP_EEP | SPW_CAP_TIME_CODE | SPW_CAP_STATISTICS;\n',
    '    capabilities.bits = SPW_CAP_EEP | SPW_CAP_TIME_CODE | SPW_CAP_STATISTICS |\n'
    '                        SPW_CAP_RATE_CONTROL | SPW_CAP_FAULT_INJECTION;\n',
)

old_send = '''spw_result_t UdpBackend::send_datagram(const std::uint8_t* bytes,
                                       std::size_t size,
                                       spw_timeout_us_t timeout_us) noexcept {
    if (socket_fd_ < 0) {
        return SPW_ERR_INVALID_STATE;
    }

    pollfd descriptor{};
    descriptor.fd = socket_fd_;
    descriptor.events = POLLOUT;
    int ready = 0;
    do {
        ready = ::poll(&descriptor, 1, timeout_ms(timeout_us));
    } while (ready < 0 && errno == EINTR);
    if (ready == 0) {
        return SPW_ERR_TIMEOUT;
    }
    if (ready < 0 || (descriptor.revents & POLLOUT) == 0) {
        return SPW_ERR_BACKEND;
    }

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(config_.remote_port);
    if (::inet_pton(AF_INET, config_.remote_address, &remote.sin_addr) != 1) {
        return SPW_ERR_BACKEND;
    }

    const ssize_t sent = ::sendto(socket_fd_, bytes, size, 0,
                                  reinterpret_cast<const sockaddr*>(&remote),
                                  sizeof(remote));
    return sent == static_cast<ssize_t>(size) ? SPW_OK : SPW_ERR_BACKEND;
}
'''
new_send = '''spw_result_t UdpBackend::send_datagram_raw(const std::uint8_t* bytes,
                                           std::size_t size,
                                           spw_timeout_us_t timeout_us) noexcept {
    if (socket_fd_ < 0) {
        return SPW_ERR_INVALID_STATE;
    }

    pollfd descriptor{};
    descriptor.fd = socket_fd_;
    descriptor.events = POLLOUT;
    int ready = 0;
    do {
        ready = ::poll(&descriptor, 1, timeout_ms(timeout_us));
    } while (ready < 0 && errno == EINTR);
    if (ready == 0) {
        return SPW_ERR_TIMEOUT;
    }
    if (ready < 0 || (descriptor.revents & POLLOUT) == 0) {
        return SPW_ERR_BACKEND;
    }

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(config_.remote_port);
    if (::inet_pton(AF_INET, config_.remote_address, &remote.sin_addr) != 1) {
        return SPW_ERR_BACKEND;
    }

    const ssize_t sent = ::sendto(socket_fd_, bytes, size, 0,
                                  reinterpret_cast<const sockaddr*>(&remote),
                                  sizeof(remote));
    return sent == static_cast<ssize_t>(size) ? SPW_OK : SPW_ERR_BACKEND;
}

spw_result_t UdpBackend::wait_transport_fault_delay(
    std::uint32_t delay_us, spw_timeout_us_t timeout_us) noexcept {
    if (delay_us == 0u) {
        return SPW_OK;
    }
    if (timeout_us != SPW_TIMEOUT_INFINITE && timeout_us < delay_us) {
        return SPW_ERR_TIMEOUT;
    }

    timespec request{};
    request.tv_sec = static_cast<time_t>(delay_us / 1000000u);
    request.tv_nsec = static_cast<long>((delay_us % 1000000u) * 1000u);
    timespec remaining{};
    while (::nanosleep(&request, &remaining) != 0) {
        if (errno != EINTR) {
            return SPW_ERR_BACKEND;
        }
        request = remaining;
    }
    return SPW_OK;
}

spw_result_t UdpBackend::send_datagram(const std::uint8_t* bytes,
                                       std::size_t size,
                                       spw_timeout_us_t timeout_us) noexcept {
    if (reordered_datagram_valid_) {
        const spw_result_t current = send_datagram_raw(bytes, size, timeout_us);
        if (current != SPW_OK) {
            return current;
        }
        const spw_result_t held = send_datagram_raw(
            reordered_datagram_.data(), reordered_datagram_size_, timeout_us);
        clear_reordered_datagram();
        return held;
    }

    Header header{};
    if (decode_header(bytes, size, header) != DecodeResult::Ok) {
        return send_datagram_raw(bytes, size, timeout_us);
    }

    const FaultInjector::Decision decision = fault_injector_.transport(header.type);
    switch (decision.action) {
    case SPW_UDP_FAULT_ACTION_TRANSPORT_DROP:
        ++fault_statistics_.transport_drops;
        ++statistics_.dropped_packets;
        return SPW_OK;

    case SPW_UDP_FAULT_ACTION_TRANSPORT_DUPLICATE: {
        ++fault_statistics_.transport_duplicates;
        const spw_result_t first = send_datagram_raw(bytes, size, timeout_us);
        return first == SPW_OK ? send_datagram_raw(bytes, size, timeout_us) : first;
    }

    case SPW_UDP_FAULT_ACTION_TRANSPORT_REORDER:
        ++fault_statistics_.transport_reorders;
        if (size > reordered_datagram_.size()) {
            return SPW_ERR_BACKEND;
        }
        std::memcpy(reordered_datagram_.data(), bytes, size);
        reordered_datagram_size_ = size;
        reordered_datagram_valid_ = true;
        return SPW_OK;

    case SPW_UDP_FAULT_ACTION_TRANSPORT_DELAY: {
        ++fault_statistics_.transport_delays;
        const spw_result_t delay = wait_transport_fault_delay(
            decision.delay_us, timeout_us);
        return delay == SPW_OK ? send_datagram_raw(bytes, size, timeout_us) : delay;
    }

    case SPW_UDP_FAULT_ACTION_NONE:
    case SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP:
        return send_datagram_raw(bytes, size, timeout_us);
    }
    return send_datagram_raw(bytes, size, timeout_us);
}
'''
replace("src/backends/ethernet/udp_backend.cpp", old_send, new_send)

replace(
    "src/backends/ethernet/udp_backend.cpp",
    '    if (packet.length != 0u) {\n'
    '        std::memcpy(pending_tx_packet_.data(), packet.data, packet.length);\n'
    '    }\n'
    '    pending_tx_packet_size_ = packet.length;\n'
    '    pending_tx_terminator_ = packet.terminator;\n',
    '    spw_terminator_t effective_terminator = packet.terminator;\n'
    '    if (packet.terminator == SPW_TERMINATOR_EOP && fault_injector_.spacewire_eep()) {\n'
    '        effective_terminator = SPW_TERMINATOR_EEP;\n'
    '        ++fault_statistics_.spacewire_eep_injections;\n'
    '    }\n\n'
    '    if (packet.length != 0u) {\n'
    '        std::memcpy(pending_tx_packet_.data(), packet.data, packet.length);\n'
    '    }\n'
    '    pending_tx_packet_size_ = packet.length;\n'
    '    pending_tx_terminator_ = effective_terminator;\n',
)
replace(
    "src/backends/ethernet/udp_backend.cpp",
    '    if (packet.terminator == SPW_TERMINATOR_EEP) {\n'
    '        statistics_.eep_packets++;\n'
    '    }\n',
    '    if (pending_tx_terminator_ == SPW_TERMINATOR_EEP) {\n'
    '        statistics_.eep_packets++;\n'
    '    }\n',
)
replace(
    "src/backends/ethernet/udp_backend.cpp",
    'spw_result_t UdpBackend::clear_statistics() noexcept {\n'
    '    statistics_ = {};\n'
    '    return SPW_OK;\n'
    '}\n\n'
    '} // namespace spwkit::detail',
    'spw_result_t UdpBackend::clear_statistics() noexcept {\n'
    '    statistics_ = {};\n'
    '    return SPW_OK;\n'
    '}\n\n'
    'spw_result_t UdpBackend::get_fault_statistics(\n'
    '    spw_fault_statistics_t& statistics) const noexcept {\n'
    '    statistics = fault_statistics_;\n'
    '    return SPW_OK;\n'
    '}\n\n'
    'spw_result_t UdpBackend::clear_fault_statistics() noexcept {\n'
    '    fault_statistics_ = {};\n'
    '    return SPW_OK;\n'
    '}\n\n'
    '} // namespace spwkit::detail',
)

# Direct deterministic rule-engine test.
Path("tests/deterministic_faults.cpp").write_text(r'''// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/deterministic_faults.hpp"

#include <spwkit/udp.h>

#include <cassert>

using spwkit::ethernet::DeterministicFaultInjector;
using spwkit::ethernet::vspw_tp::MessageType;

int main() {
    spw_udp_config_t config = SPW_UDP_CONFIG_INITIALIZER(1u, 2u, 3u);
    config.fault_seed = 0x123456789abcdef0ull;

    config.fault_rules[0] = {
        SPW_UDP_FAULT_ACTION_TRANSPORT_DROP,
        SPW_UDP_FAULT_TARGET_DATA,
        SPW_UDP_FAULT_PROBABILITY_SCALE,
        1u,
        0u,
        0u,
    };
    config.fault_rules[1] = {
        SPW_UDP_FAULT_ACTION_TRANSPORT_DUPLICATE,
        SPW_UDP_FAULT_TARGET_ACK,
        2500u,
        0u,
        0u,
        0u,
    };
    config.fault_rules[2] = {
        SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP,
        SPW_UDP_FAULT_TARGET_DATA,
        SPW_UDP_FAULT_PROBABILITY_SCALE,
        1u,
        0u,
        0u,
    };

    assert(DeterministicFaultInjector::valid_rule(config.fault_rules[0]));
    assert(DeterministicFaultInjector::valid_rule(config.fault_rules[1]));
    assert(DeterministicFaultInjector::valid_rule(config.fault_rules[2]));

    DeterministicFaultInjector faults(config);
    assert(faults.transport(MessageType::Data).action ==
           SPW_UDP_FAULT_ACTION_TRANSPORT_DROP);
    assert(faults.transport(MessageType::Data).action == SPW_UDP_FAULT_ACTION_NONE);
    assert(faults.spacewire_eep());
    assert(!faults.spacewire_eep());

    DeterministicFaultInjector a(config);
    DeterministicFaultInjector b(config);
    for (unsigned i = 0u; i < 64u; ++i) {
        assert(a.transport(MessageType::Ack).action ==
               b.transport(MessageType::Ack).action);
    }

    spw_udp_fault_rule_t invalid = config.fault_rules[0];
    invalid.probability_per_10000 = SPW_UDP_FAULT_PROBABILITY_SCALE + 1u;
    assert(!DeterministicFaultInjector::valid_rule(invalid));
    invalid = config.fault_rules[0];
    invalid.delay_us = 1u;
    assert(!DeterministicFaultInjector::valid_rule(invalid));
    invalid = config.fault_rules[2];
    invalid.target = SPW_UDP_FAULT_TARGET_ACK;
    assert(!DeterministicFaultInjector::valid_rule(invalid));
    return 0;
}
''')

# Real UDP integration scenarios for each fault domain.
Path("tests/udp_faults.cpp").write_text(r'''// SPDX-License-Identifier: Apache-2.0
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
''')

# Test registration.
replace(
    "tests/CMakeLists.txt",
    'add_executable(spwkit_virtual_link_timing_test virtual_link_timing.cpp)\n'
    'target_link_libraries(spwkit_virtual_link_timing_test PRIVATE SpWKit::spwkit)\n'
    'target_include_directories(spwkit_virtual_link_timing_test PRIVATE ${PROJECT_SOURCE_DIR}/src)\n'
    'target_compile_features(spwkit_virtual_link_timing_test PRIVATE cxx_std_17)\n'
    'spwkit_apply_no_throw_policy(spwkit_virtual_link_timing_test)\n',
    'add_executable(spwkit_virtual_link_timing_test virtual_link_timing.cpp)\n'
    'target_link_libraries(spwkit_virtual_link_timing_test PRIVATE SpWKit::spwkit)\n'
    'target_include_directories(spwkit_virtual_link_timing_test PRIVATE ${PROJECT_SOURCE_DIR}/src)\n'
    'target_compile_features(spwkit_virtual_link_timing_test PRIVATE cxx_std_17)\n'
    'spwkit_apply_no_throw_policy(spwkit_virtual_link_timing_test)\n\n'
    'add_executable(spwkit_deterministic_faults_test deterministic_faults.cpp)\n'
    'target_link_libraries(spwkit_deterministic_faults_test PRIVATE SpWKit::spwkit)\n'
    'target_include_directories(spwkit_deterministic_faults_test PRIVATE ${PROJECT_SOURCE_DIR}/src)\n'
    'target_compile_features(spwkit_deterministic_faults_test PRIVATE cxx_std_17)\n'
    'spwkit_apply_no_throw_policy(spwkit_deterministic_faults_test)\n',
)
replace(
    "tests/CMakeLists.txt",
    '    add_executable(spwkit_udp_timing_test udp_timing.cpp)\n'
    '    target_link_libraries(spwkit_udp_timing_test PRIVATE SpWKit::spwkit)\n'
    '    target_compile_features(spwkit_udp_timing_test PRIVATE cxx_std_17)\n',
    '    add_executable(spwkit_udp_timing_test udp_timing.cpp)\n'
    '    target_link_libraries(spwkit_udp_timing_test PRIVATE SpWKit::spwkit)\n'
    '    target_compile_features(spwkit_udp_timing_test PRIVATE cxx_std_17)\n\n'
    '    add_executable(spwkit_udp_faults_test udp_faults.cpp)\n'
    '    target_link_libraries(spwkit_udp_faults_test PRIVATE SpWKit::spwkit)\n'
    '    target_compile_features(spwkit_udp_faults_test PRIVATE cxx_std_17)\n',
)
replace(
    "tests/CMakeLists.txt",
    'add_test(NAME virtual_link_timing COMMAND spwkit_virtual_link_timing_test)\n'
    'set_tests_properties(virtual_link_timing PROPERTIES LABELS "unit;transport;timing")\n',
    'add_test(NAME virtual_link_timing COMMAND spwkit_virtual_link_timing_test)\n'
    'set_tests_properties(virtual_link_timing PROPERTIES LABELS "unit;transport;timing")\n'
    'add_test(NAME deterministic_fault_rules COMMAND spwkit_deterministic_faults_test)\n'
    'set_tests_properties(deterministic_fault_rules PROPERTIES LABELS "unit;transport;fault")\n',
)
replace(
    "tests/CMakeLists.txt",
    '    add_test(NAME udp_virtual_link_timing COMMAND spwkit_udp_timing_test)\n'
    '    set_tests_properties(udp_virtual_link_timing PROPERTIES LABELS "transport;integration;d2d;timing")\n',
    '    add_test(NAME udp_virtual_link_timing COMMAND spwkit_udp_timing_test)\n'
    '    set_tests_properties(udp_virtual_link_timing PROPERTIES LABELS "transport;integration;d2d;timing")\n'
    '    add_test(NAME udp_deterministic_faults COMMAND spwkit_udp_faults_test)\n'
    '    set_tests_properties(udp_deterministic_faults PROPERTIES LABELS "transport;integration;d2d;fault;edge")\n',
)
