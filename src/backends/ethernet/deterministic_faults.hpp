// SPDX-License-Identifier: Apache-2.0
#pragma once

/* C++ test compatibility over the single C11 deterministic fault engine. */
#include "backends/ethernet/deterministic_faults.h"
#include "backends/ethernet/vspw_tp.hpp"

namespace spwkit::ethernet {

class DeterministicFaultInjector {
public:
    struct Decision {
        spw_udp_fault_action_t action{SPW_UDP_FAULT_ACTION_NONE};
        std::uint32_t delay_us{0u};
        constexpr bool injected() const noexcept {
            return action != SPW_UDP_FAULT_ACTION_NONE;
        }
    };

    explicit DeterministicFaultInjector(const spw_udp_config_t& config) noexcept {
        spw_fault_injector_init(&state_, &config);
    }

    void reset() noexcept { spw_fault_injector_reset(&state_); }

    Decision transport(vspw_tp::MessageType type) noexcept {
        const auto decision = spw_fault_inject_transport(
            &state_, static_cast<std::uint8_t>(type));
        return {decision.action, decision.delay_us};
    }

    bool spacewire_eep() noexcept {
        return spw_fault_inject_spacewire_eep(&state_);
    }

    static bool valid_rule(const spw_udp_fault_rule_t& rule) noexcept {
        return spw_fault_rule_valid(&rule);
    }

private:
    spw_deterministic_fault_injector_t state_{};
};

} // namespace spwkit::ethernet
