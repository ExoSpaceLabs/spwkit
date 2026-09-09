// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/fragment_reassembler.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>

namespace {

using spwkit::ethernet::FragmentReassembler;
using namespace spwkit::ethernet::vspw_tp;

Header fragment(std::uint32_t message_id,
                std::uint32_t offset,
                std::uint16_t payload_size,
                std::uint32_t total_size,
                std::uint8_t flags) {
    Header header{};
    header.type = MessageType::Data;
    header.flags = flags;
    header.payload_size = payload_size;
    header.link_id = 1u;
    header.session_id = 0x1122334455667788ull;
    header.sequence = offset + 1u;
    header.message_id = message_id;
    header.fragment_offset = offset;
    header.total_size = total_size;
    return header;
}

} // namespace

int main() {
    using Reassembler = FragmentReassembler<32u>;
    using Result = Reassembler::Result;

    constexpr std::uint8_t eop_ack = FlagEop | FlagAckRequired;
    std::array<std::uint8_t, 12> bytes{};
    for (std::size_t i = 0u; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(0x30u + i);
    }

    Reassembler reassembly{};

    /* Reverse-order delivery must complete only after every byte and both boundaries arrive. */
    Header end = fragment(7u, 8u, 4u, 12u,
                          static_cast<std::uint8_t>(eop_ack | FlagFragmentEnd));
    Header start = fragment(7u, 0u, 4u, 12u,
                            static_cast<std::uint8_t>(eop_ack | FlagFragmentStart));
    Header middle = fragment(7u, 4u, 4u, 12u, eop_ack);

    assert(reassembly.push(end, bytes.data() + 8u) == Result::Accepted);
    assert(reassembly.push(start, bytes.data()) == Result::Accepted);
    assert(reassembly.push(middle, bytes.data() + 4u) == Result::Complete);
    assert(reassembly.active());
    assert(reassembly.message_id() == 7u);
    assert(reassembly.size() == bytes.size());
    assert(!reassembly.eep());
    assert(reassembly.ack_required());
    assert(std::memcmp(reassembly.data(), bytes.data(), bytes.size()) == 0);

    /* Exact duplicate bytes are idempotent. */
    reassembly.reset();
    assert(reassembly.push(start, bytes.data()) == Result::Accepted);
    assert(reassembly.push(start, bytes.data()) == Result::Duplicate);

    /* Identical partial overlap contributes only previously unseen bytes. */
    Header overlap = fragment(7u, 2u, 4u, 12u, eop_ack);
    assert(reassembly.push(overlap, bytes.data() + 2u) == Result::Accepted);

    /* A conflicting overlap must not partially mutate the buffer. */
    const std::array<std::uint8_t, 4> conflict{{bytes[2], 0xffu, bytes[4], bytes[5]}};
    assert(reassembly.push(overlap, conflict.data()) == Result::Conflict);
    assert(reassembly.data()[3] == bytes[3]);

    /* Inconsistent metadata cannot replace an active logical packet. */
    Header wrong_message = fragment(8u, 6u, 2u, 12u, eop_ack);
    assert(reassembly.push(wrong_message, bytes.data() + 6u) == Result::Conflict);
    Header wrong_total = fragment(7u, 6u, 2u, 13u, eop_ack);
    assert(reassembly.push(wrong_total, bytes.data() + 6u) == Result::Conflict);
    Header wrong_terminator = fragment(
        7u, 6u, 2u, 12u,
        static_cast<std::uint8_t>(FlagEep | FlagAckRequired));
    assert(reassembly.push(wrong_terminator, bytes.data() + 6u) == Result::Conflict);

    /* Full byte coverage without both boundary markers is not a complete packet. */
    reassembly.reset();
    Header first_no_start = fragment(9u, 0u, 6u, 12u, eop_ack);
    Header second_no_end = fragment(9u, 6u, 6u, 12u, eop_ack);
    assert(reassembly.push(first_no_start, bytes.data()) == Result::Accepted);
    assert(reassembly.push(second_no_end, bytes.data() + 6u) == Result::Accepted);

    Header start_marker = fragment(
        9u, 0u, 6u, 12u,
        static_cast<std::uint8_t>(eop_ack | FlagFragmentStart));
    Header end_marker = fragment(
        9u, 6u, 6u, 12u,
        static_cast<std::uint8_t>(eop_ack | FlagFragmentEnd));
    assert(reassembly.push(start_marker, bytes.data()) == Result::Duplicate);
    assert(reassembly.push(end_marker, bytes.data() + 6u) == Result::Complete);

    /* Invalid fragmented identities are rejected before state is created. */
    reassembly.reset();
    Header zero_message = fragment(0u, 0u, 4u, 12u,
                                   static_cast<std::uint8_t>(eop_ack | FlagFragmentStart));
    assert(reassembly.push(zero_message, bytes.data()) == Result::Invalid);
    assert(!reassembly.active());

    /* Non-overlapping ranges crossing 64-bit coverage-word boundaries use the
     * fast path and remain reusable after reset. */
    {
        using WideReassembler = FragmentReassembler<256u>;
        using WideResult = WideReassembler::Result;
        std::array<std::uint8_t, 130> wide_bytes{};
        WideReassembler wide{};
        for (std::size_t i = 0u; i < wide_bytes.size(); ++i) {
            wide_bytes[i] = static_cast<std::uint8_t>((i * 13u + 5u) & 0xffu);
        }

        Header wide_start = fragment(
            10u, 0u, 70u, static_cast<std::uint32_t>(wide_bytes.size()),
            static_cast<std::uint8_t>(eop_ack | FlagFragmentStart));
        Header wide_end = fragment(
            10u, 70u, 60u, static_cast<std::uint32_t>(wide_bytes.size()),
            static_cast<std::uint8_t>(eop_ack | FlagFragmentEnd));
        assert(wide.push(wide_start, wide_bytes.data()) == WideResult::Accepted);
        assert(wide.push(wide_end, wide_bytes.data() + 70u) == WideResult::Complete);
        assert(std::memcmp(wide.data(), wide_bytes.data(), wide_bytes.size()) == 0);

        wide.reset();
        wide_start.message_id = 11u;
        wide_end.message_id = 11u;
        assert(wide.push(wide_end, wide_bytes.data() + 70u) == WideResult::Accepted);
        assert(wide.push(wide_start, wide_bytes.data()) == WideResult::Complete);
        assert(std::memcmp(wide.data(), wide_bytes.data(), wide_bytes.size()) == 0);
    }

    return 0;
}
