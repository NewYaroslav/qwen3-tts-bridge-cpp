#pragma once

/// \file ControlCodec.hpp
/// \brief JSON codec for protocol v1 control_json and error_json payloads.

#include <cstddef>
#include <vector>

#include <qwen_tts_bridge/protocol/control/ControlMessages.hpp>

namespace qwen_tts_bridge {

/// \brief Decodes a UTF-8 control_json payload.
///
/// The payload is expected to come from a frame parser that already validated
/// the type-specific control payload size limit.
///
/// \param data Payload bytes.
/// \param size Payload size in bytes.
/// \param direction Expected message direction.
/// \return Decoded message or a JSON-level protocol error.
[[nodiscard]] ControlDecodeResult decode_control_message(
    const std::byte* data,
    std::size_t size,
    ControlMessageDirection direction);

/// \brief Decodes a UTF-8 control_json payload.
///
/// The payload is expected to come from a frame parser that already validated
/// the type-specific control payload size limit.
///
/// \param payload Payload bytes.
/// \param direction Expected message direction.
/// \return Decoded message or a JSON-level protocol error.
[[nodiscard]] ControlDecodeResult decode_control_message(
    const std::vector<std::byte>& payload,
    ControlMessageDirection direction);

/// \brief Encodes a control message into compact UTF-8 JSON payload bytes.
/// \param message Control message DTO.
/// \return Payload bytes or an encode error.
[[nodiscard]] JsonPayloadEncodeResult encode_control_message(
    const ControlMessage& message);

/// \brief Decodes a UTF-8 error_json payload.
///
/// The payload is expected to come from a frame parser that already validated
/// the type-specific error payload size limit.
///
/// \param data Payload bytes.
/// \param size Payload size in bytes.
/// \return Decoded worker error payload or a JSON-level protocol error.
[[nodiscard]] ErrorDecodeResult decode_error_message(
    const std::byte* data,
    std::size_t size);

/// \brief Decodes a UTF-8 error_json payload.
///
/// The payload is expected to come from a frame parser that already validated
/// the type-specific error payload size limit.
///
/// \param payload Payload bytes.
/// \return Decoded worker error payload or a JSON-level protocol error.
[[nodiscard]] ErrorDecodeResult decode_error_message(
    const std::vector<std::byte>& payload);

/// \brief Encodes a worker error into compact UTF-8 JSON payload bytes.
/// \param message Error DTO.
/// \return Payload bytes or an encode error.
[[nodiscard]] JsonPayloadEncodeResult encode_error_message(
    const ErrorMessage& message);

} // namespace qwen_tts_bridge
