// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstddef>
#include <cstdint>

namespace spwkit::ethernet::vspw_tp {

inline constexpr std::uint32_t kMagic = 0x56535057u; // "VSPW"
inline constexpr std::uint8_t kVersionMajor = 1u;
inline constexpr std::uint8_t kVersionMinor = 0u;
inline constexpr std::size_t kHeaderSize = 32u;
inline constexpr std::size_t kMaxUdpPayload = 65507u;
inline constexpr std::size_t kMaxFragmentPayload = kMaxUdpPayload - kHeaderSize;
inline constexpr std::uint32_t kMaxPacketSize = 16u * 1024u * 1024u;
inline constexpr std::size_t kTimeCodePayloadSize = 2u;
inline constexpr std::size_t kKeepalivePayloadSize = 8u;
inline constexpr std::size_t kAckPayloadSize = 8u;

enum class MessageType : std::uint8_t {
    Data = 1u,
    TimeCode = 2u,
    LinkControl = 3u,
    Keepalive = 4u,
    Ack = 5u,
};

enum Flag : std::uint8_t {
    FlagNone = 0u,
    FlagEop = 1u << 0u,
    FlagEep = 1u << 1u,
    FlagFragmentStart = 1u << 2u,
    FlagFragmentEnd = 1u << 3u,
    FlagAckRequired = 1u << 4u,
};

struct Header {
    std::uint8_t version_major{kVersionMajor};
    std::uint8_t version_minor{kVersionMinor};
    MessageType type{MessageType::Data};
    std::uint8_t flags{FlagNone};
    std::uint16_t header_size{static_cast<std::uint16_t>(kHeaderSize)};
    std::uint16_t payload_size{0u};
    std::uint32_t link_id{0u};
    std::uint32_t sequence{0u};
    std::uint32_t message_id{0u};
    std::uint32_t fragment_offset{0u};
    std::uint32_t total_size{0u};
};

enum class DecodeResult : std::uint8_t {
    Ok = 0u,
    BufferTooSmall,
    BadMagic,
    UnsupportedVersion,
    UnsupportedType,
    InvalidHeaderSize,
    InvalidPayloadSize,
    InvalidFlags,
    InvalidFragment,
};

bool is_known_type(MessageType type) noexcept;
bool validate(const Header& header) noexcept;

bool encode_header(const Header& header,
                   std::uint8_t* destination,
                   std::size_t destination_size) noexcept;

DecodeResult decode_header(const std::uint8_t* source,
                           std::size_t source_size,
                           Header& out_header) noexcept;

bool encode_keepalive_payload(std::uint64_t session_id,
                              std::uint8_t* destination,
                              std::size_t destination_size) noexcept;

bool decode_keepalive_payload(const std::uint8_t* source,
                              std::size_t source_size,
                              std::uint64_t& session_id) noexcept;

bool encode_ack_payload(std::uint64_t acknowledged_session_id,
                        std::uint8_t* destination,
                        std::size_t destination_size) noexcept;

bool decode_ack_payload(const std::uint8_t* source,
                        std::size_t source_size,
                        std::uint64_t& acknowledged_session_id) noexcept;

} // namespace spwkit::ethernet::vspw_tp
