// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/vspw_tp.hpp"

#include <array>
#include <cassert>
#include <cstdint>

using namespace spwkit::ethernet::vspw_tp;

int main() {
    Header header{};
    header.type = MessageType::Data;
    header.flags = FlagEep;
    header.payload_size = 4u;
    header.link_id = 0x01020304u;
    header.sequence = 0x11223344u;
    header.message_id = 0x55667788u;
    header.total_size = 4u;

    std::array<std::uint8_t, kHeaderSize + 4u> frame{};
    assert(encode_header(header, frame.data(), frame.size()));

    const std::array<std::uint8_t, kHeaderSize> expected{{
        0x56, 0x53, 0x50, 0x57,
        0x01, 0x00, 0x01, 0x02,
        0x00, 0x20, 0x00, 0x04,
        0x01, 0x02, 0x03, 0x04,
        0x11, 0x22, 0x33, 0x44,
        0x55, 0x66, 0x77, 0x88,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x04,
    }};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        assert(frame[i] == expected[i]);
    }

    Header decoded{};
    assert(decode_header(frame.data(), frame.size(), decoded) == DecodeResult::Ok);
    assert(decoded.type == MessageType::Data);
    assert(decoded.flags == FlagEep);
    assert(decoded.payload_size == 4u);
    assert(decoded.link_id == 0x01020304u);
    assert(decoded.sequence == 0x11223344u);
    assert(decoded.message_id == 0x55667788u);
    assert(decoded.fragment_offset == 0u);
    assert(decoded.total_size == 4u);

    auto malformed = frame;
    malformed[0] = 0u;
    assert(decode_header(malformed.data(), malformed.size(), decoded) == DecodeResult::BadMagic);

    malformed = frame;
    malformed[4] = 2u;
    assert(decode_header(malformed.data(), malformed.size(), decoded) == DecodeResult::UnsupportedVersion);

    malformed = frame;
    malformed[6] = 0xffu;
    assert(decode_header(malformed.data(), malformed.size(), decoded) == DecodeResult::UnsupportedType);

    malformed = frame;
    malformed[7] = static_cast<std::uint8_t>(FlagEop | FlagEep);
    assert(decode_header(malformed.data(), malformed.size(), decoded) == DecodeResult::InvalidFlags);

    Header fragment{};
    fragment.type = MessageType::Data;
    fragment.flags = FlagFragmentStart;
    fragment.payload_size = 1200u;
    fragment.link_id = 7u;
    fragment.sequence = 11u;
    fragment.message_id = 42u;
    fragment.fragment_offset = 0u;
    fragment.total_size = 4096u;
    std::array<std::uint8_t, kHeaderSize + 1200u> fragment_frame{};
    assert(encode_header(fragment, fragment_frame.data(), fragment_frame.size()));
    assert(decode_header(fragment_frame.data(), fragment_frame.size(), decoded) == DecodeResult::Ok);

    fragment.flags = FlagFragmentEnd;
    fragment.payload_size = 496u;
    fragment.fragment_offset = 3600u;
    std::array<std::uint8_t, kHeaderSize + 496u> end_frame{};
    assert(encode_header(fragment, end_frame.data(), end_frame.size()));
    assert(decode_header(end_frame.data(), end_frame.size(), decoded) == DecodeResult::Ok);

    fragment.fragment_offset = 3500u;
    assert(!encode_header(fragment, end_frame.data(), end_frame.size()));

    Header time_code{};
    time_code.type = MessageType::TimeCode;
    time_code.payload_size = 2u;
    time_code.total_size = 2u;
    std::array<std::uint8_t, kHeaderSize + 2u> tc_frame{};
    assert(encode_header(time_code, tc_frame.data(), tc_frame.size()));
    assert(decode_header(tc_frame.data(), tc_frame.size(), decoded) == DecodeResult::Ok);

    time_code.flags = FlagEop;
    assert(!encode_header(time_code, tc_frame.data(), tc_frame.size()));

    assert(decode_header(frame.data(), kHeaderSize - 1u, decoded) == DecodeResult::BufferTooSmall);
    return 0;
}
