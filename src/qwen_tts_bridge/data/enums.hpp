#pragma once

/// \file enums.hpp
/// \brief Enumerations used by QwenTTSBridge protocol data structures.

#include <cstdint>

namespace qwen_tts_bridge {

/// \enum FrameType
/// \brief Binary frame payload category used by protocol v1.
enum class FrameType : std::uint16_t {
    ControlJson = 1, ///< UTF-8 JSON control message.
    AudioPcm = 2,    ///< Raw PCM audio payload.
    ErrorJson = 3    ///< UTF-8 JSON worker error message.
};

/// \enum ParseStatus
/// \brief Result class returned by the streaming frame parser.
enum class ParseStatus {
    NeedMoreData, ///< More bytes are required before a full frame is available.
    FrameReady,   ///< A complete frame was parsed successfully.
    FatalError    ///< A framing-level protocol error made the stream invalid.
};

/// \enum ProtocolError
/// \brief Fatal binary framing errors detected before JSON-level validation.
enum class ProtocolError {
    None,                         ///< No protocol error.
    InvalidMagic,                 ///< Frame magic does not match QTB1.
    UnsupportedProtocolVersion,   ///< Frame protocol_version is unsupported.
    HeaderTooSmall,               ///< header_size is below the v1 minimum.
    HeaderTooLarge,               ///< header_size exceeds the v1 maximum.
    UnknownFrameType,             ///< frame_type is not defined by v1.
    UnsupportedFlags,             ///< flags contains a non-zero unsupported value.
    PayloadTooLarge               ///< payload_size exceeds frame or type limits.
};

} // namespace qwen_tts_bridge
