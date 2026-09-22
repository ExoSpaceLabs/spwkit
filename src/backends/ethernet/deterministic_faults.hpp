// SPDX-License-Identifier: Apache-2.0
#pragma once

/* C++ test compatibility over the single C11 deterministic fault engine. */
#include "backends/ethernet/deterministic_faults.h"
#include "backends/ethernet/vspw_tp.hpp"

#include <array>
#include <cstdint>

namespace spwkit::ethernet {

class DeterministicFaultInjector {
public:
    using Rule = spw_vspw_fault_rule_t;
    using Rules = std::array<Rule, SPW_VSPW_FAULT_RULE_COUNT>;

    struct Decision {
        spw_vspw_fault_action_t action{SPW_VSPW_FAULT_ACTION_NONE};
        std::uint32_t delay_us{0u};
        constexpr bool injected() const noexcept {
            return action != SPW_VSPW_FAULT_ACTION_NONE;
        }
    };

    DeterministicFaultInjector(const Rules& rules, std::uint64_t seed) noexcept {
        spw_fault_injector_init(&state_, rules.data(), rules.size(), seed);
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

    static bool valid_rule(const Rule& rule) noexcept {
        return spw_fault_rule_valid(&rule);
    }

private:
    spw_deterministic_fault_injector_t state_{};
};

} // namespace spwkit::ethernet
