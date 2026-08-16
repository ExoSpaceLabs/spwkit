// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/deterministic_faults.h"

#include <stddef.h>
#include <string.h>

static bool is_transport_action(spw_udp_fault_action_t action) {
    return action >= SPW_UDP_FAULT_ACTION_TRANSPORT_DROP &&
           action <= SPW_UDP_FAULT_ACTION_TRANSPORT_DELAY;
}

static bool target_matches(spw_udp_fault_target_t configured,
                           spw_udp_fault_target_t actual) {
    return configured == SPW_UDP_FAULT_TARGET_ANY || configured == actual;
}

static spw_udp_fault_target_t target_for(spw_vspw_tp_message_type_t type) {
    switch (type) {
    case SPW_VSPW_TP_DATA:
        return SPW_UDP_FAULT_TARGET_DATA;
    case SPW_VSPW_TP_TIME_CODE:
        return SPW_UDP_FAULT_TARGET_TIME_CODE;
    case SPW_VSPW_TP_ACK:
        return SPW_UDP_FAULT_TARGET_ACK;
    case SPW_VSPW_TP_KEEPALIVE:
    case SPW_VSPW_TP_LINK_CONTROL:
        return SPW_UDP_FAULT_TARGET_KEEPALIVE;
    default:
        return SPW_UDP_FAULT_TARGET_ANY;
    }
}

bool spw_fault_rule_valid(const spw_udp_fault_rule_t* rule) {
    if (rule == NULL || rule->reserved != 0u ||
        rule->probability_per_10000 > SPW_UDP_FAULT_PROBABILITY_SCALE ||
        rule->target > SPW_UDP_FAULT_TARGET_KEEPALIVE ||
        rule->action > SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP) {
        return false;
    }
    if (rule->action == SPW_UDP_FAULT_ACTION_NONE) {
        return rule->target == SPW_UDP_FAULT_TARGET_ANY &&
               rule->probability_per_10000 == 0u && rule->max_events == 0u &&
               rule->delay_us == 0u;
    }
    if (rule->probability_per_10000 == 0u) {
        return false;
    }
    if (rule->action == SPW_UDP_FAULT_ACTION_TRANSPORT_DELAY) {
        return rule->delay_us != 0u;
    }
    if (rule->delay_us != 0u) {
        return false;
    }
    if (rule->action == SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP) {
        return rule->target == SPW_UDP_FAULT_TARGET_DATA;
    }
    return true;
}

void spw_fault_injector_reset(spw_deterministic_fault_injector_t* injector) {
    size_t i;
    if (injector == NULL) {
        return;
    }
    for (i = 0u; i < SPW_UDP_FAULT_RULE_COUNT; ++i) {
        uint64_t state = injector->seed ^
            (UINT64_C(0x9e3779b97f4a7c15) * (uint64_t)(i + 1u));
        injector->states[i] =
            state == 0u ? UINT64_C(0xd1b54a32d192ed03) + i : state;
        injector->injected_counts[i] = 0u;
    }
}

void spw_fault_injector_init(spw_deterministic_fault_injector_t* injector,
                             const spw_udp_config_t* config) {
    if (injector == NULL || config == NULL) {
        return;
    }
    memset(injector, 0, sizeof(*injector));
    memcpy(injector->rules, config->fault_rules, sizeof(injector->rules));
    injector->seed = config->fault_seed;
    spw_fault_injector_reset(injector);
}

static bool exhausted(const spw_deterministic_fault_injector_t* injector,
                      size_t index,
                      const spw_udp_fault_rule_t* rule) {
    return rule->max_events != 0u &&
           injector->injected_counts[index] >= rule->max_events;
}

static bool fires(spw_deterministic_fault_injector_t* injector,
                  size_t index,
                  const spw_udp_fault_rule_t* rule) {
    uint64_t x = injector->states[index];
    x ^= x << 13u;
    x ^= x >> 7u;
    x ^= x << 17u;
    injector->states[index] = x;
    if (rule->probability_per_10000 >= SPW_UDP_FAULT_PROBABILITY_SCALE) {
        return true;
    }
    return (x % SPW_UDP_FAULT_PROBABILITY_SCALE) <
           rule->probability_per_10000;
}

spw_fault_decision_t spw_fault_inject_transport(
    spw_deterministic_fault_injector_t* injector,
    spw_vspw_tp_message_type_t type) {
    spw_fault_decision_t decision = {SPW_UDP_FAULT_ACTION_NONE, 0u};
    const spw_udp_fault_target_t target = target_for(type);
    size_t i;
    if (injector == NULL) {
        return decision;
    }
    for (i = 0u; i < SPW_UDP_FAULT_RULE_COUNT; ++i) {
        const spw_udp_fault_rule_t* rule = &injector->rules[i];
        if (!is_transport_action(rule->action) ||
            !target_matches(rule->target, target) ||
            exhausted(injector, i, rule)) {
            continue;
        }
        if (fires(injector, i, rule)) {
            ++injector->injected_counts[i];
            decision.action = rule->action;
            decision.delay_us = rule->delay_us;
            return decision;
        }
    }
    return decision;
}

bool spw_fault_inject_spacewire_eep(
    spw_deterministic_fault_injector_t* injector) {
    size_t i;
    if (injector == NULL) {
        return false;
    }
    for (i = 0u; i < SPW_UDP_FAULT_RULE_COUNT; ++i) {
        const spw_udp_fault_rule_t* rule = &injector->rules[i];
        if (rule->action != SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP ||
            !target_matches(rule->target, SPW_UDP_FAULT_TARGET_DATA) ||
            exhausted(injector, i, rule)) {
            continue;
        }
        if (fires(injector, i, rule)) {
            ++injector->injected_counts[i];
            return true;
        }
    }
    return false;
}
