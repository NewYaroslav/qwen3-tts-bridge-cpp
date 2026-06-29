#pragma once

/// \file FrameCodec.hpp
/// \brief Binary frame encoding and frame-type rules for protocol v1.

#include <cstddef>
#include <cstdint>
#include <vector>

#include <qwen_tts_bridge/data.hpp>

namespace qwen_tts_bridge {

/// \brief Checks whether a raw frame type value is defined by protocol v1.
/// \param value Raw integer frame type.
/// \return True when value maps to a known FrameType.
bool is_known_frame_type(std::uint16_t value);

/// \brief Returns the maximum allowed payload size for the frame type.
/// \param frame_type Frame payload category.
/// \return Payload limit in bytes.
std::uint32_t max_payload_size(FrameType frame_type);

/// \brief Encodes a protocol frame with a default v1 header.
/// \param frame_type Payload category.
/// \param request_id Session or request identifier.
/// \param payload Payload bytes.
/// \return Encode result with binary frame bytes when successful.
EncodeResult encode_frame(
    FrameType frame_type,
    RequestId request_id,
    const std::vector<std::byte>& payload);

/// \brief Encodes a protocol frame with an explicit fixed-size v1 header.
///
/// Protocol v1 senders cannot emit header extension bytes. Therefore
/// `header.header_size` must be exactly `ProtocolLimits::min_header_size`.
/// `header.payload_size` must also match the payload size.
///
/// \param header Header values to encode.
/// \param payload Payload bytes.
/// \return Encode result with binary frame bytes when successful.
EncodeResult encode_frame(
    const FrameHeader& header,
    const std::vector<std::byte>& payload);

} // namespace qwen_tts_bridge
