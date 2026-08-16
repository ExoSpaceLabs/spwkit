// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "backends/ethernet/vspw_tp.hpp"

#include <spwkit/udp.h>

#include <array>
#include <cstddef>
#include <cstdint>

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

    explicit DeterministicFaultInjector(const spw_udp_config_t& config) noexcept
        : seed_(config.fault_seed) {
        for (std::size_t i = 0u; i < rules_.size(); ++i) {
            rules_[i] = config.fault_rules[i];
        }
        reset();
    }

    void reset() noexcept {
        for (std::size_t i = 0u; i < states_.size(); ++i) {
            std::uint64_t state = seed_ ^
                (0x9e3779b97f4a7c15ull * static_cast<std::uint64_t>(i + 1u));
            states_[i] = state == 0u ? (0xd1b54a32d192ed03ull + i) : state;
            injected_counts_[i] = 0u;
        }
    }

    Decision transport(vspw_tp::MessageType type) noexcept {
        const spw_udp_fault_target_t target = target_for(type);
        for (std::size_t i = 0u; i < rules_.size(); ++i) {
            const auto& rule = rules_[i];
            if (!is_transport_action(rule.action) ||
                !target_matches(rule.target, target) ||
                exhausted(i, rule)) {
                continue;
            }
            if (fires(i, rule)) {
                ++injected_counts_[i];
                return {rule.action, rule.delay_us};
            }
        }
        return {};
    }

    bool spacewire_eep() noexcept {
        for (std::size_t i = 0u; i < rules_.size(); ++i) {
            const auto& rule = rules_[i];
            if (rule.action != SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP ||
                !target_matches(rule.target, SPW_UDP_FAULT_TARGET_DATA) ||
                exhausted(i, rule)) {
                continue;
            }
            if (fires(i, rule)) {
                ++injected_counts_[i];
                return true;
            }
        }
        return false;
    }

    static bool valid_rule(const spw_udp_fault_rule_t& rule) noexcept {
        if (rule.reserved != 0u ||
            rule.probability_per_10000 > SPW_UDP_FAULT_PROBABILITY_SCALE ||
            rule.target > SPW_UDP_FAULT_TARGET_KEEPALIVE ||
            rule.action > SPW_UDP_FAULT_ACTION_SPACEWIRE_EEP) {
            return false;
        }
        if (rule.action == SPW_UDP_FAULT_ACTION_NONE) {
            return rule.target == SPW_UDP_FAULT_TARGET_ANY &&
                   rule.probability_per_10000 == 0u &&
                   rule.max_events == 0u && rule.delay_us == 0u;
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

private:
    static bool is_transport_action(spw_udp_fault_action_t action) noexcept {
        return action >= SPW_UDP_FAULT_ACTION_TRANSPORT_DROP &&
               action <= SPW_UDP_FAULT_ACTION_TRANSPORT_DELAY;
    }

    static bool target_matches(spw_udp_fault_target_t configured,
                               spw_udp_fault_target_t actual) noexcept {
        return configured == SPW_UDP_FAULT_TARGET_ANY || configured == actual;
    }

    static spw_udp_fault_target_t target_for(vspw_tp::MessageType type) noexcept {
        switch (type) {
        case vspw_tp::MessageType::Data:
            return SPW_UDP_FAULT_TARGET_DATA;
        case vspw_tp::MessageType::TimeCode:
            return SPW_UDP_FAULT_TARGET_TIME_CODE;
        case vspw_tp::MessageType::Ack:
            return SPW_UDP_FAULT_TARGET_ACK;
        case vspw_tp::MessageType::Keepalive:
        case vspw_tp::MessageType::LinkControl:
            return SPW_UDP_FAULT_TARGET_KEEPALIVE;
        }
        return SPW_UDP_FAULT_TARGET_ANY;
    }

    bool exhausted(std::size_t index, const spw_udp_fault_rule_t& rule) const noexcept {
        return rule.max_events != 0u && injected_counts_[index] >= rule.max_events;
    }

    bool fires(std::size_t index, const spw_udp_fault_rule_t& rule) noexcept {
        std::uint64_t x = states_[index];
        x ^= x << 13u;
        x ^= x >> 7u;
        x ^= x << 17u;
        states_[index] = x;
        if (rule.probability_per_10000 >= SPW_UDP_FAULT_PROBABILITY_SCALE) {
            return true;
        }
        return (x % SPW_UDP_FAULT_PROBABILITY_SCALE) < rule.probability_per_10000;
    }

    std::array<spw_udp_fault_rule_t, SPW_UDP_FAULT_RULE_COUNT> rules_{};
    std::array<std::uint64_t, SPW_UDP_FAULT_RULE_COUNT> states_{};
    std::array<std::uint32_t, SPW_UDP_FAULT_RULE_COUNT> injected_counts_{};
    std::uint64_t seed_{0u};
};

} // namespace spwkit::ethernet
