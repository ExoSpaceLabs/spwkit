// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "backends/ethernet/vspw_tp.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace spwkit::ethernet {

/*
 * Fixed-capacity DATA reassembly independent of socket I/O.
 *
 * Coverage is tracked with one bit per payload byte. This costs Capacity / 8
 * bytes and allows exact duplicate/overlap handling without imposing a hidden
 * maximum number of fragment ranges. Payload storage and bookkeeping are both
 * deterministic and allocation-free.
 */
template <std::size_t Capacity>
class FragmentReassembler {
public:
    enum class Result : std::uint8_t {
        Accepted = 0u,
        Duplicate,
        Complete,
        Conflict,
        Invalid,
    };

    void reset() noexcept {
        coverage_.fill(0u);
        covered_bytes_ = 0u;
        message_id_ = 0u;
        total_size_ = 0u;
        eep_ = false;
        ack_required_ = false;
        seen_start_ = false;
        seen_end_ = false;
        active_ = false;
    }

    bool active() const noexcept {
        return active_;
    }

    std::uint32_t message_id() const noexcept {
        return message_id_;
    }

    std::size_t size() const noexcept {
        return total_size_;
    }

    const std::uint8_t* data() const noexcept {
        return data_.data();
    }

    bool eep() const noexcept {
        return eep_;
    }

    bool ack_required() const noexcept {
        return ack_required_;
    }

    Result push(const vspw_tp::Header& header,
                const std::uint8_t* payload) noexcept {
        using namespace vspw_tp;

        if (header.type != MessageType::Data ||
            header.total_size == header.payload_size ||
            header.total_size == 0u || header.total_size > Capacity ||
            header.message_id == 0u ||
            (header.payload_size != 0u && payload == nullptr) ||
            static_cast<std::uint64_t>(header.fragment_offset) +
                    header.payload_size >
                header.total_size) {
            return Result::Invalid;
        }

        const bool eep = (header.flags & FlagEep) != 0u;
        const bool ack_required = (header.flags & FlagAckRequired) != 0u;
        const bool start = (header.flags & FlagFragmentStart) != 0u;
        const bool end = (header.flags & FlagFragmentEnd) != 0u;

        if (!active_) {
            active_ = true;
            message_id_ = header.message_id;
            total_size_ = header.total_size;
            eep_ = eep;
            ack_required_ = ack_required;
        } else if (header.message_id != message_id_ ||
                   header.total_size != total_size_ || eep != eep_ ||
                   ack_required != ack_required_) {
            return Result::Conflict;
        }

        /*
         * Validate all already-covered bytes before modifying state so a
         * conflicting overlap cannot partially mutate the reassembly buffer.
         */
        for (std::size_t i = 0u; i < header.payload_size; ++i) {
            const std::size_t position =
                static_cast<std::size_t>(header.fragment_offset) + i;
            if (is_covered(position) && data_[position] != payload[i]) {
                return Result::Conflict;
            }
        }

        std::size_t added = 0u;
        for (std::size_t i = 0u; i < header.payload_size; ++i) {
            const std::size_t position =
                static_cast<std::size_t>(header.fragment_offset) + i;
            if (!is_covered(position)) {
                data_[position] = payload[i];
                mark_covered(position);
                ++covered_bytes_;
                ++added;
            }
        }

        seen_start_ = seen_start_ || start;
        seen_end_ = seen_end_ || end;

        if (covered_bytes_ == total_size_ && seen_start_ && seen_end_) {
            return Result::Complete;
        }
        return added == 0u ? Result::Duplicate : Result::Accepted;
    }

private:
    static constexpr std::size_t coverage_word_bits = 64u;
    static constexpr std::size_t coverage_words =
        (Capacity + coverage_word_bits - 1u) / coverage_word_bits;

    bool is_covered(std::size_t position) const noexcept {
        const std::size_t word = position / coverage_word_bits;
        const std::size_t bit = position % coverage_word_bits;
        return (coverage_[word] & (std::uint64_t{1u} << bit)) != 0u;
    }

    void mark_covered(std::size_t position) noexcept {
        const std::size_t word = position / coverage_word_bits;
        const std::size_t bit = position % coverage_word_bits;
        coverage_[word] |= std::uint64_t{1u} << bit;
    }

    std::array<std::uint8_t, Capacity> data_{};
    std::array<std::uint64_t, coverage_words> coverage_{};
    std::size_t covered_bytes_{0u};
    std::uint32_t message_id_{0u};
    std::uint32_t total_size_{0u};
    bool eep_{false};
    bool ack_required_{false};
    bool seen_start_{false};
    bool seen_end_{false};
    bool active_{false};
};

} // namespace spwkit::ethernet
