// SPDX-License-Identifier: Apache-2.0
#pragma once

/*
 * Transitional C++ test compatibility over the C11 VSPW-TP implementation.
 * Runtime code uses vspw_tp.h directly. This wrapper keeps the mature v0.2
 * protocol tests source-stable while ensuring there is only one codec.
 */
#include "backends/ethernet/vspw_tp.h"

#include <cstddef>
#include <cstdint>

namespace spwkit::ethernet::vspw_tp {

inline constexpr std::uint32_t kMagic = SPW_VSPW_TP_MAGIC;
inline constexpr std::uint8_t kVersionMajor = SPW_VSPW_TP_VERSION_MAJOR;
inline constexpr std::uint8_t kVersionMinor = SPW_VSPW_TP_VERSION_MINOR;
inline constexpr std::size_t kHeaderSize = SPW_VSPW_TP_HEADER_SIZE;
inline constexpr std::size_t kMaxUdpPayload = SPW_VSPW_TP_MAX_UDP_PAYLOAD;
inline constexpr std::size_t kMaxFragmentPayload = SPW_VSPW_TP_MAX_FRAGMENT_PAYLOAD;
inline constexpr std::uint32_t kMaxPacketSize = SPW_VSPW_TP_MAX_PACKET_SIZE;
inline constexpr std::size_t kTimeCodePayloadSize = SPW_VSPW_TP_TIME_CODE_PAYLOAD_SIZE;
inline constexpr std::size_t kAckPayloadSize = SPW_VSPW_TP_ACK_PAYLOAD_SIZE;

enum class MessageType : std::uint8_t {
    Data = SPW_VSPW_TP_DATA,
    TimeCode = SPW_VSPW_TP_TIME_CODE,
    LinkControl = SPW_VSPW_TP_LINK_CONTROL,
    Keepalive = SPW_VSPW_TP_KEEPALIVE,
    Ack = SPW_VSPW_TP_ACK,
};

enum Flag : std::uint8_t {
    FlagNone = SPW_VSPW_TP_FLAG_NONE,
    FlagEop = SPW_VSPW_TP_FLAG_EOP,
    FlagEep = SPW_VSPW_TP_FLAG_EEP,
    FlagFragmentStart = SPW_VSPW_TP_FLAG_FRAGMENT_START,
    FlagFragmentEnd = SPW_VSPW_TP_FLAG_FRAGMENT_END,
    FlagAckRequired = SPW_VSPW_TP_FLAG_ACK_REQUIRED,
};

struct Header {
    std::uint8_t version_major{kVersionMajor};
    std::uint8_t version_minor{kVersionMinor};
    MessageType type{MessageType::Data};
    std::uint8_t flags{FlagNone};
    std::uint16_t header_size{static_cast<std::uint16_t>(kHeaderSize)};
    std::uint16_t payload_size{0u};
    std::uint32_t link_id{0u};
    std::uint64_t session_id{0u};
    std::uint32_t sequence{0u};
    std::uint32_t message_id{0u};
    std::uint32_t fragment_offset{0u};
    std::uint32_t total_size{0u};
};

enum class DecodeResult : std::uint8_t {
    Ok = SPW_VSPW_TP_DECODE_OK,
    BufferTooSmall = SPW_VSPW_TP_DECODE_BUFFER_TOO_SMALL,
    BadMagic = SPW_VSPW_TP_DECODE_BAD_MAGIC,
    UnsupportedVersion = SPW_VSPW_TP_DECODE_UNSUPPORTED_VERSION,
    UnsupportedType = SPW_VSPW_TP_DECODE_UNSUPPORTED_TYPE,
    InvalidHeaderSize = SPW_VSPW_TP_DECODE_INVALID_HEADER_SIZE,
    InvalidPayloadSize = SPW_VSPW_TP_DECODE_INVALID_PAYLOAD_SIZE,
    InvalidSession = SPW_VSPW_TP_DECODE_INVALID_SESSION,
    InvalidFlags = SPW_VSPW_TP_DECODE_INVALID_FLAGS,
    InvalidFragment = SPW_VSPW_TP_DECODE_INVALID_FRAGMENT,
};

inline spw_vspw_tp_header_t to_c(const Header& header) noexcept {
    spw_vspw_tp_header_t result{};
    result.version_major = header.version_major;
    result.version_minor = header.version_minor;
    result.type = static_cast<std::uint8_t>(header.type);
    result.flags = header.flags;
    result.header_size = header.header_size;
    result.payload_size = header.payload_size;
    result.link_id = header.link_id;
    result.session_id = header.session_id;
    result.sequence = header.sequence;
    result.message_id = header.message_id;
    result.fragment_offset = header.fragment_offset;
    result.total_size = header.total_size;
    return result;
}

inline Header from_c(const spw_vspw_tp_header_t& header) noexcept {
    Header result{};
    result.version_major = header.version_major;
    result.version_minor = header.version_minor;
    result.type = static_cast<MessageType>(header.type);
    result.flags = header.flags;
    result.header_size = header.header_size;
    result.payload_size = header.payload_size;
    result.link_id = header.link_id;
    result.session_id = header.session_id;
    result.sequence = header.sequence;
    result.message_id = header.message_id;
    result.fragment_offset = header.fragment_offset;
    result.total_size = header.total_size;
    return result;
}

inline bool is_known_type(MessageType type) noexcept {
    return spw_vspw_tp_is_known_type(static_cast<std::uint8_t>(type));
}

inline bool validate(const Header& header) noexcept {
    const auto value = to_c(header);
    return spw_vspw_tp_validate(&value);
}

inline bool encode_header(const Header& header,
                          std::uint8_t* destination,
                          std::size_t destination_size) noexcept {
    const auto value = to_c(header);
    return spw_vspw_tp_encode_header(&value, destination, destination_size);
}

inline DecodeResult decode_header(const std::uint8_t* source,
                                  std::size_t source_size,
                                  Header& out_header) noexcept {
    spw_vspw_tp_header_t value{};
    const auto result = spw_vspw_tp_decode_header(source, source_size, &value);
    if (result == SPW_VSPW_TP_DECODE_OK) {
        out_header = from_c(value);
    }
    return static_cast<DecodeResult>(result);
}

inline bool encode_ack_payload(std::uint64_t acknowledged_session_id,
                               std::uint8_t* destination,
                               std::size_t destination_size) noexcept {
    return spw_vspw_tp_encode_ack_payload(acknowledged_session_id, destination,
                                          destination_size);
}

inline bool decode_ack_payload(const std::uint8_t* source,
                               std::size_t source_size,
                               std::uint64_t& acknowledged_session_id) noexcept {
    return spw_vspw_tp_decode_ack_payload(source, source_size,
                                          &acknowledged_session_id);
}

} // namespace spwkit::ethernet::vspw_tp
