// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/deterministic_faults.hpp"

#include <cassert>

using spwkit::ethernet::DeterministicFaultInjector;
using spwkit::ethernet::vspw_tp::MessageType;

int main() {
    DeterministicFaultInjector::Rules rules{};
    constexpr auto seed = 0x123456789abcdef0ull;

    rules[0] = {
        SPW_VSPW_FAULT_ACTION_TRANSPORT_DROP,
        SPW_VSPW_FAULT_TARGET_DATA,
        SPW_VSPW_FAULT_PROBABILITY_SCALE,
        1u,
        0u,
        0u,
    };
    rules[1] = {
        SPW_VSPW_FAULT_ACTION_TRANSPORT_DUPLICATE,
        SPW_VSPW_FAULT_TARGET_ACK,
        2500u,
        0u,
        0u,
        0u,
    };
    rules[2] = {
        SPW_VSPW_FAULT_ACTION_SPACEWIRE_EEP,
        SPW_VSPW_FAULT_TARGET_DATA,
        SPW_VSPW_FAULT_PROBABILITY_SCALE,
        1u,
        0u,
        0u,
    };

    assert(DeterministicFaultInjector::valid_rule(rules[0]));
    assert(DeterministicFaultInjector::valid_rule(rules[1]));
    assert(DeterministicFaultInjector::valid_rule(rules[2]));

    DeterministicFaultInjector faults(rules, seed);
    assert(faults.transport(MessageType::Data).action ==
           SPW_VSPW_FAULT_ACTION_TRANSPORT_DROP);
    assert(faults.transport(MessageType::Data).action ==
           SPW_VSPW_FAULT_ACTION_NONE);
    assert(faults.spacewire_eep());
    assert(!faults.spacewire_eep());

    DeterministicFaultInjector a(rules, seed);
    DeterministicFaultInjector b(rules, seed);
    for (unsigned i = 0u; i < 64u; ++i) {
        assert(a.transport(MessageType::Ack).action ==
               b.transport(MessageType::Ack).action);
    }

    spw_vspw_fault_rule_t invalid = rules[0];
    invalid.probability_per_10000 = SPW_VSPW_FAULT_PROBABILITY_SCALE + 1u;
    assert(!DeterministicFaultInjector::valid_rule(invalid));
    invalid = rules[0];
    invalid.delay_us = 1u;
    assert(!DeterministicFaultInjector::valid_rule(invalid));
    invalid = rules[2];
    invalid.target = SPW_VSPW_FAULT_TARGET_ACK;
    assert(!DeterministicFaultInjector::valid_rule(invalid));
    return 0;
}
