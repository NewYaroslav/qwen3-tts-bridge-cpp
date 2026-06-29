#pragma once

/// \file protocol_types.hpp
/// \brief DTOs and constants for QwenTTSBridge protocol v1 framing.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <qwen_tts_bridge/data/enums.hpp>

namespace qwen_tts_bridge {

/// \brief Request identifier used to route frames and callbacks.
using RequestId = std::uint64_t;

/// \struct ProtocolLimits
/// \brief Protocol v1 constants shared by frame encoders and parsers.
struct ProtocolLimits {
    static constexpr std::uint16_t protocol_version = 1;               ///< Supported protocol version.
    static constexpr std::uint16_t min_header_size = 24;               ///< Fixed v1 header size.
    static constexpr std::uint16_t max_header_size = 256;              ///< Maximum compatible header size.
    static constexpr std::uint32_t max_control_payload_bytes = 1024u * 1024u;      ///< Control JSON limit.
    static constexpr std::uint32_t max_audio_payload_bytes = 16u * 1024u * 1024u;  ///< Audio PCM hard limit.
    static constexpr std::uint32_t max_error_payload_bytes = 1024u * 1024u;        ///< Error JSON limit.
    static constexpr std::uint32_t max_frame_payload_bytes = 16u * 1024u * 1024u;  ///< Absolute payload limit.
};

/// \struct FrameHeader
/// \brief Parsed binary header that precedes every protocol frame.
struct FrameHeader {
    std::uint16_t protocol_version = ProtocolLimits::protocol_version; ///< Header protocol version.
    std::uint16_t header_size = ProtocolLimits::min_header_size;       ///< Full header size in bytes.
    FrameType frame_type = FrameType::ControlJson;                     ///< Payload category.
    std::uint16_t flags = 0;                                           ///< Reserved for future v1 extensions.
    std::uint32_t payload_size = 0;                                    ///< Number of payload bytes after header.
    RequestId request_id = 0;                                          ///< Session or request identifier.
};

/// \struct Frame
/// \brief Complete parsed frame with header and payload bytes.
struct Frame {
    FrameHeader header;             ///< Parsed binary header.
    std::vector<std::byte> payload; ///< Payload bytes.
};

/// \struct ParseResult
/// \brief Result returned by FrameParser::parse_next().
struct ParseResult {
    ParseStatus status = ParseStatus::NeedMoreData; ///< Parser status.
    ProtocolError error = ProtocolError::None;      ///< Fatal error category, if any.
    std::string message;                            ///< Human-readable diagnostic text.
    Frame frame;                                    ///< Parsed frame when status is FrameReady.
};

} // namespace qwen_tts_bridge
