// SPDX-License-Identifier: Apache-2.0
#include "backends/ethernet/vspw_tp.hpp"

namespace spwkit::ethernet::vspw_tp {
namespace {

void write_u16(std::uint8_t* dst, std::uint16_t value) noexcept {
    dst[0] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    dst[1] = static_cast<std::uint8_t>(value & 0xffu);
}

void write_u32(std::uint8_t* dst, std::uint32_t value) noexcept {
    dst[0] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
    dst[1] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    dst[2] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    dst[3] = static_cast<std::uint8_t>(value & 0xffu);
}

void write_u64(std::uint8_t* dst, std::uint64_t value) noexcept {
    write_u32(dst, static_cast<std::uint32_t>(value >> 32u));
    write_u32(dst + 4u, static_cast<std::uint32_t>(value & 0xffffffffu));
}

std::uint16_t read_u16(const std::uint8_t* src) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(src[0]) << 8u) |
                                      static_cast<std::uint16_t>(src[1]));
}

std::uint32_t read_u32(const std::uint8_t* src) noexcept {
    return (static_cast<std::uint32_t>(src[0]) << 24u) |
           (static_cast<std::uint32_t>(src[1]) << 16u) |
           (static_cast<std::uint32_t>(src[2]) << 8u) |
           static_cast<std::uint32_t>(src[3]);
}

std::uint64_t read_u64(const std::uint8_t* src) noexcept {
    return (static_cast<std::uint64_t>(read_u32(src)) << 32u) |
           static_cast<std::uint64_t>(read_u32(src + 4u));
}

bool has_flag(std::uint8_t flags, Flag flag) noexcept {
    return (flags & static_cast<std::uint8_t>(flag)) != 0u;
}

bool validate_flags(const Header& header) noexcept {
    constexpr std::uint8_t known_flags = FlagEop | FlagEep | FlagFragmentStart |
                                         FlagFragmentEnd | FlagAckRequired;
    if ((header.flags & static_cast<std::uint8_t>(~known_flags)) != 0u) {
        return false;
    }

    if (has_flag(header.flags, FlagEop) && has_flag(header.flags, FlagEep)) {
        return false;
    }

    if (header.type != MessageType::Data &&
        (has_flag(header.flags, FlagEop) || has_flag(header.flags, FlagEep))) {
        return false;
    }

    if (has_flag(header.flags, FlagAckRequired) &&
        header.type != MessageType::Data && header.type != MessageType::TimeCode) {
        return false;
    }

    return true;
}

bool validate_fragment(const Header& header) noexcept {
    const bool start = has_flag(header.flags, FlagFragmentStart);
    const bool end = has_flag(header.flags, FlagFragmentEnd);

    if (header.total_size > kMaxPacketSize) {
        return false;
    }
    if (header.payload_size > kMaxFragmentPayload) {
        return false;
    }
    if (header.fragment_offset > header.total_size) {
        return false;
    }
    if (static_cast<std::uint64_t>(header.fragment_offset) + header.payload_size >
        header.total_size) {
        return false;
    }

    if (header.type != MessageType::Data) {
        return header.fragment_offset == 0u &&
               header.total_size == header.payload_size && !start && !end;
    }

    if (header.total_size == header.payload_size) {
        return header.fragment_offset == 0u && !start && !end;
    }

    if (!start && !end && header.payload_size == 0u) {
        return false;
    }
    if (start && header.fragment_offset != 0u) {
        return false;
    }
    if (end && static_cast<std::uint64_t>(header.fragment_offset) +
                   header.payload_size != header.total_size) {
        return false;
    }
    return true;
}

bool validate_message_shape(const Header& header) noexcept {
    switch (header.type) {
        case MessageType::Data:
            return true;
        case MessageType::TimeCode:
            return header.payload_size == kTimeCodePayloadSize &&
                   header.total_size == kTimeCodePayloadSize;
        case MessageType::Keepalive:
            return header.payload_size == kKeepalivePayloadSize &&
                   header.total_size == kKeepalivePayloadSize;
        case MessageType::Ack:
            return header.payload_size == kAckPayloadSize &&
                   header.total_size == kAckPayloadSize &&
                   header.message_id != 0u;
        case MessageType::LinkControl:
            return true;
    }
    return false;
}

bool encode_nonzero_u64(std::uint64_t value,
                        std::uint8_t* destination,
                        std::size_t destination_size,
                        std::size_t required_size) noexcept {
    if (destination == nullptr || destination_size < required_size || value == 0u) {
        return false;
    }
    write_u64(destination, value);
    return true;
}

bool decode_nonzero_u64(const std::uint8_t* source,
                        std::size_t source_size,
                        std::size_t required_size,
                        std::uint64_t& value) noexcept {
    if (source == nullptr || source_size != required_size) {
        return false;
    }
    const std::uint64_t decoded = read_u64(source);
    if (decoded == 0u) {
        return false;
    }
    value = decoded;
    return true;
}

} // namespace

bool is_known_type(MessageType type) noexcept {
    switch (type) {
        case MessageType::Data:
        case MessageType::TimeCode:
        case MessageType::LinkControl:
        case MessageType::Keepalive:
        case MessageType::Ack:
            return true;
    }
    return false;
}

bool validate(const Header& header) noexcept {
    return header.version_major == kVersionMajor &&
           header.version_minor <= kVersionMinor &&
           is_known_type(header.type) &&
           header.header_size == kHeaderSize &&
           validate_flags(header) &&
           validate_fragment(header) &&
           validate_message_shape(header);
}

bool encode_header(const Header& header,
                   std::uint8_t* destination,
                   std::size_t destination_size) noexcept {
    if (destination == nullptr || destination_size < kHeaderSize || !validate(header)) {
        return false;
    }

    write_u32(destination + 0u, kMagic);
    destination[4] = header.version_major;
    destination[5] = header.version_minor;
    destination[6] = static_cast<std::uint8_t>(header.type);
    destination[7] = header.flags;
    write_u16(destination + 8u, header.header_size);
    write_u16(destination + 10u, header.payload_size);
    write_u32(destination + 12u, header.link_id);
    write_u32(destination + 16u, header.sequence);
    write_u32(destination + 20u, header.message_id);
    write_u32(destination + 24u, header.fragment_offset);
    write_u32(destination + 28u, header.total_size);
    return true;
}

DecodeResult decode_header(const std::uint8_t* source,
                           std::size_t source_size,
                           Header& out_header) noexcept {
    if (source == nullptr || source_size < kHeaderSize) {
        return DecodeResult::BufferTooSmall;
    }
    if (read_u32(source + 0u) != kMagic) {
        return DecodeResult::BadMagic;
    }

    Header header{};
    header.version_major = source[4];
    header.version_minor = source[5];
    header.type = static_cast<MessageType>(source[6]);
    header.flags = source[7];
    header.header_size = read_u16(source + 8u);
    header.payload_size = read_u16(source + 10u);
    header.link_id = read_u32(source + 12u);
    header.sequence = read_u32(source + 16u);
    header.message_id = read_u32(source + 20u);
    header.fragment_offset = read_u32(source + 24u);
    header.total_size = read_u32(source + 28u);

    if (header.version_major != kVersionMajor || header.version_minor > kVersionMinor) {
        return DecodeResult::UnsupportedVersion;
    }
    if (!is_known_type(header.type)) {
        return DecodeResult::UnsupportedType;
    }
    if (header.header_size != kHeaderSize) {
        return DecodeResult::InvalidHeaderSize;
    }
    if (header.payload_size > kMaxFragmentPayload ||
        source_size < kHeaderSize + header.payload_size) {
        return DecodeResult::InvalidPayloadSize;
    }
    if (!validate_flags(header)) {
        return DecodeResult::InvalidFlags;
    }
    if (!validate_fragment(header) || !validate_message_shape(header)) {
        return DecodeResult::InvalidFragment;
    }

    out_header = header;
    return DecodeResult::Ok;
}

bool encode_keepalive_payload(std::uint64_t session_id,
                              std::uint8_t* destination,
                              std::size_t destination_size) noexcept {
    return encode_nonzero_u64(session_id, destination, destination_size,
                              kKeepalivePayloadSize);
}

bool decode_keepalive_payload(const std::uint8_t* source,
                              std::size_t source_size,
                              std::uint64_t& session_id) noexcept {
    return decode_nonzero_u64(source, source_size, kKeepalivePayloadSize, session_id);
}

bool encode_ack_payload(std::uint64_t acknowledged_session_id,
                        std::uint8_t* destination,
                        std::size_t destination_size) noexcept {
    return encode_nonzero_u64(acknowledged_session_id, destination, destination_size,
                              kAckPayloadSize);
}

bool decode_ack_payload(const std::uint8_t* source,
                        std::size_t source_size,
                        std::uint64_t& acknowledged_session_id) noexcept {
    return decode_nonzero_u64(source, source_size, kAckPayloadSize,
                              acknowledged_session_id);
}

} // namespace spwkit::ethernet::vspw_tp
