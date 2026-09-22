// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_DETERMINISTIC_FAULTS_H
#define SPWKIT_DETERMINISTIC_FAULTS_H

#include "backends/ethernet/vspw_tp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPW_VSPW_FAULT_RULE_COUNT 8u
#define SPW_VSPW_FAULT_PROBABILITY_SCALE 10000u

typedef uint8_t spw_vspw_fault_action_t;
enum {
    SPW_VSPW_FAULT_ACTION_NONE = 0u,
    SPW_VSPW_FAULT_ACTION_TRANSPORT_DROP = 1u,
    SPW_VSPW_FAULT_ACTION_TRANSPORT_DUPLICATE = 2u,
    SPW_VSPW_FAULT_ACTION_TRANSPORT_REORDER = 3u,
    SPW_VSPW_FAULT_ACTION_TRANSPORT_DELAY = 4u,
    SPW_VSPW_FAULT_ACTION_SPACEWIRE_EEP = 5u
};

typedef uint8_t spw_vspw_fault_target_t;
enum {
    SPW_VSPW_FAULT_TARGET_ANY = 0u,
    SPW_VSPW_FAULT_TARGET_DATA = 1u,
    SPW_VSPW_FAULT_TARGET_TIME_CODE = 2u,
    SPW_VSPW_FAULT_TARGET_ACK = 3u,
    SPW_VSPW_FAULT_TARGET_KEEPALIVE = 4u
};

typedef struct spw_vspw_fault_rule {
    spw_vspw_fault_action_t action;
    spw_vspw_fault_target_t target;
    uint16_t probability_per_10000;
    uint32_t max_events;
    uint32_t delay_us;
    uint32_t reserved;
} spw_vspw_fault_rule_t;

typedef struct spw_fault_decision {
    spw_vspw_fault_action_t action;
    uint32_t delay_us;
} spw_fault_decision_t;

typedef struct spw_deterministic_fault_injector {
    spw_vspw_fault_rule_t rules[SPW_VSPW_FAULT_RULE_COUNT];
    uint64_t states[SPW_VSPW_FAULT_RULE_COUNT];
    uint32_t injected_counts[SPW_VSPW_FAULT_RULE_COUNT];
    uint64_t seed;
} spw_deterministic_fault_injector_t;

bool spw_fault_rule_valid(const spw_vspw_fault_rule_t* rule);
void spw_fault_injector_init(
    spw_deterministic_fault_injector_t* injector,
    const spw_vspw_fault_rule_t* rules,
    size_t rule_count,
    uint64_t seed);
void spw_fault_injector_reset(spw_deterministic_fault_injector_t* injector);
spw_fault_decision_t spw_fault_inject_transport(
    spw_deterministic_fault_injector_t* injector,
    spw_vspw_tp_message_type_t type);
bool spw_fault_inject_spacewire_eep(
    spw_deterministic_fault_injector_t* injector);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SPWKIT_DETERMINISTIC_FAULTS_H */
