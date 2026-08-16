// SPDX-License-Identifier: Apache-2.0
#pragma once

/* C++ test compatibility over the single C11 reassembly implementation. */
#include "backends/ethernet/fragment_reassembler.h"
#include "backends/ethernet/vspw_tp.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace spwkit::ethernet {

template <std::size_t Capacity>
class FragmentReassembler {
public:
    enum class Result : std::uint8_t {
        Accepted = SPW_REASSEMBLY_ACCEPTED,
        Duplicate = SPW_REASSEMBLY_DUPLICATE,
        Complete = SPW_REASSEMBLY_COMPLETE,
        Conflict = SPW_REASSEMBLY_CONFLICT,
        Invalid = SPW_REASSEMBLY_INVALID,
    };

    FragmentReassembler() noexcept {
        spw_fragment_reassembler_init(&state_, data_.data(), Capacity,
                                      coverage_.data(), coverage_.size());
    }

    void reset() noexcept { spw_fragment_reassembler_reset(&state_); }
    bool active() const noexcept { return state_.active; }
    std::uint32_t message_id() const noexcept { return state_.message_id; }
    std::size_t size() const noexcept { return state_.total_size; }
    const std::uint8_t* data() const noexcept { return data_.data(); }
    bool eep() const noexcept {
        return (state_.terminator_flags & SPW_VSPW_TP_FLAG_EEP) != 0u;
    }
    bool ack_required() const noexcept { return state_.ack_required; }

    Result push(const vspw_tp::Header& header,
                const std::uint8_t* payload) noexcept {
        const auto c_header = vspw_tp::to_c(header);
        return static_cast<Result>(
            spw_fragment_reassembler_push(&state_, &c_header, payload));
    }

private:
    std::array<std::uint8_t, Capacity> data_{};
    std::array<std::uint64_t, SPW_FRAGMENT_COVERAGE_WORDS(Capacity)> coverage_{};
    spw_fragment_reassembler_t state_{};
};

} // namespace spwkit::ethernet
