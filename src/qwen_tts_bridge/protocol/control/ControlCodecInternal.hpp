#pragma once

/// \file ControlCodecInternal.hpp
/// \brief Internal JSON helpers for the protocol control codec.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ControlMessages.hpp"
#include "ControlCodec.hpp"

namespace qwen_tts_bridge::control_detail {

using Json = nlohmann::json;

inline constexpr const char* kMessageType = "message_type";
inline constexpr const char* kProtocolVersion = "protocol_version";
inline constexpr const char* kRequestId = "request_id";

std::string bytes_to_string(const std::byte* data, std::size_t size);
std::vector<std::byte> string_to_bytes(const std::string& value);

ControlDecodeResult control_error(
    ControlCodecError error,
    std::string diagnostic);
ErrorDecodeResult error_error(
    ControlCodecError error,
    std::string diagnostic);
JsonPayloadEncodeResult encode_error(
    ControlCodecError error,
    std::string diagnostic);

bool has_forbidden_header_field(const Json& object);
const Json* find_field(const Json& object, const char* name);

bool read_required_string(
    const Json& object,
    const char* name,
    std::string& out,
    std::string& diagnostic,
    ControlCodecError& error);
bool read_optional_string(
    const Json& object,
    const char* name,
    std::string& out,
    std::string& diagnostic,
    ControlCodecError& error);
bool read_optional_bool(
    const Json& object,
    const char* name,
    bool& out,
    bool& present,
    std::string& diagnostic,
    ControlCodecError& error);
bool read_required_bool(
    const Json& object,
    const char* name,
    bool& out,
    std::string& diagnostic,
    ControlCodecError& error);
bool read_optional_u64(
    const Json& object,
    const char* name,
    std::uint64_t& out,
    bool& present,
    std::string& diagnostic,
    ControlCodecError& error);
bool read_optional_u32(
    const Json& object,
    const char* name,
    std::uint32_t& out,
    bool& present,
    std::string& diagnostic,
    ControlCodecError& error);
bool read_required_u32(
    const Json& object,
    const char* name,
    std::uint32_t& out,
    std::string& diagnostic,
    ControlCodecError& error);
bool read_optional_audio_format(
    const Json& object,
    const char* name,
    AudioFormat& out,
    std::string& diagnostic,
    ControlCodecError& error);
bool read_required_audio_format(
    const Json& object,
    const char* name,
    AudioFormat& out,
    std::string& diagnostic,
    ControlCodecError& error);
bool read_capabilities(
    const Json& object,
    WorkerCapabilities& out,
    std::string& diagnostic,
    ControlCodecError& error);

Json audio_format_to_json(const AudioFormat& format);
Json capabilities_to_json(const WorkerCapabilities& capabilities);
JsonPayloadEncodeResult encode_json_payload(const Json& value);

ControlCodecError validate_non_empty_string(
    const std::string& value,
    const char* name,
    std::string& diagnostic);
ControlCodecError validate_audio_format(
    const AudioFormat& format,
    const char* name,
    std::string& diagnostic);
ControlCodecError validate_control_message(
    const ControlMessage& message,
    std::string& diagnostic);
ControlCodecError validate_error_message(
    const ErrorMessage& message,
    std::string& diagnostic);

ControlDecodeResult decode_control_payload(
    const std::byte* data,
    std::size_t size,
    ControlMessageDirection direction);
ErrorDecodeResult decode_error_payload(
    const std::byte* data,
    std::size_t size);
Json control_message_to_json(const ControlMessage& message);
Json error_message_to_json(const ErrorMessage& message);

} // namespace qwen_tts_bridge::control_detail
