// SPDX-License-Identifier: Apache-2.0
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
