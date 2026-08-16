// SPDX-License-Identifier: Apache-2.0
#ifndef SPWKIT_DETERMINISTIC_FAULTS_H
#define SPWKIT_DETERMINISTIC_FAULTS_H

#include "backends/ethernet/vspw_tp.h"

#include <spwkit/udp.h>

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct spw_fault_decision {
    spw_udp_fault_action_t action;
    uint32_t delay_us;
} spw_fault_decision_t;

typedef struct spw_deterministic_fault_injector {
    spw_udp_fault_rule_t rules[SPW_UDP_FAULT_RULE_COUNT];
    uint64_t states[SPW_UDP_FAULT_RULE_COUNT];
    uint32_t injected_counts[SPW_UDP_FAULT_RULE_COUNT];
    uint64_t seed;
} spw_deterministic_fault_injector_t;

bool spw_fault_rule_valid(const spw_udp_fault_rule_t* rule);
void spw_fault_injector_init(spw_deterministic_fault_injector_t* injector,
                             const spw_udp_config_t* config);
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
