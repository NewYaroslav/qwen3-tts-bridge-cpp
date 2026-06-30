#include "ControlCodecInternal.hpp"

#include <limits>
#include <utility>

namespace qwen_tts_bridge::control_detail {

std::string bytes_to_string(const std::byte* data, std::size_t size) {
    if (data == nullptr || size == 0) {
        return {};
    }

    return std::string(reinterpret_cast<const char*>(data), size);
}

std::vector<std::byte> string_to_bytes(const std::string& value) {
    std::vector<std::byte> bytes;
    bytes.reserve(value.size());
    for (const char ch : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return bytes;
}

ControlDecodeResult control_error(
    ControlCodecError error,
    std::string diagnostic) {
    ControlDecodeResult result;
    result.error = error;
    result.diagnostic = std::move(diagnostic);
    return result;
}

ErrorDecodeResult error_error(
    ControlCodecError error,
    std::string diagnostic) {
    ErrorDecodeResult result;
    result.error = error;
    result.diagnostic = std::move(diagnostic);
    return result;
}

JsonPayloadEncodeResult encode_error(
    ControlCodecError error,
    std::string diagnostic) {
    JsonPayloadEncodeResult result;
    result.error = error;
    result.diagnostic = std::move(diagnostic);
    return result;
}

bool has_forbidden_header_field(const Json& object) {
    return object.contains(kProtocolVersion) || object.contains(kRequestId);
}

const Json* find_field(const Json& object, const char* name) {
    const auto it = object.find(name);
    if (it == object.end()) {
        return nullptr;
    }
    return &(*it);
}

bool read_required_string(
    const Json& object,
    const char* name,
    std::string& out,
    std::string& diagnostic,
    ControlCodecError& error) {
    const Json* value = find_field(object, name);
    if (value == nullptr) {
        error = ControlCodecError::MissingRequiredField;
        diagnostic = std::string("missing required field: ") + name;
        return false;
    }
    if (!value->is_string()) {
        error = ControlCodecError::InvalidFieldType;
        diagnostic = std::string("field must be a string: ") + name;
        return false;
    }

    out = value->get<std::string>();
    return true;
}

bool read_optional_string(
    const Json& object,
    const char* name,
    std::string& out,
    std::string& diagnostic,
    ControlCodecError& error) {
    const Json* value = find_field(object, name);
    if (value == nullptr) {
        return true;
    }
    if (!value->is_string()) {
        error = ControlCodecError::InvalidFieldType;
        diagnostic = std::string("field must be a string: ") + name;
        return false;
    }

    out = value->get<std::string>();
    return true;
}

bool read_optional_bool(
    const Json& object,
    const char* name,
    bool& out,
    bool& present,
    std::string& diagnostic,
    ControlCodecError& error) {
    const Json* value = find_field(object, name);
    if (value == nullptr) {
        present = false;
        return true;
    }
    if (!value->is_boolean()) {
        error = ControlCodecError::InvalidFieldType;
        diagnostic = std::string("field must be a bool: ") + name;
        return false;
    }

    present = true;
    out = value->get<bool>();
    return true;
}

bool read_required_bool(
    const Json& object,
    const char* name,
    bool& out,
    std::string& diagnostic,
    ControlCodecError& error) {
    const Json* value = find_field(object, name);
    if (value == nullptr) {
        error = ControlCodecError::MissingRequiredField;
        diagnostic = std::string("missing required field: ") + name;
        return false;
    }
    if (!value->is_boolean()) {
        error = ControlCodecError::InvalidFieldType;
        diagnostic = std::string("field must be a bool: ") + name;
        return false;
    }

    out = value->get<bool>();
    return true;
}

bool json_to_u64(const Json& value, std::uint64_t& out) {
    if (value.is_number_unsigned()) {
        out = value.get<std::uint64_t>();
        return true;
    }
    if (!value.is_number_integer()) {
        return false;
    }

    const auto signed_value = value.get<std::int64_t>();
    if (signed_value < 0) {
        return false;
    }

    out = static_cast<std::uint64_t>(signed_value);
    return true;
}

bool read_optional_u64(
    const Json& object,
    const char* name,
    std::uint64_t& out,
    bool& present,
    std::string& diagnostic,
    ControlCodecError& error) {
    const Json* value = find_field(object, name);
    if (value == nullptr) {
        present = false;
        return true;
    }

    if (!json_to_u64(*value, out)) {
        error = ControlCodecError::InvalidFieldType;
        diagnostic = std::string("field must be a non-negative integer: ") + name;
        return false;
    }

    present = true;
    return true;
}

bool read_optional_u32(
    const Json& object,
    const char* name,
    std::uint32_t& out,
    bool& present,
    std::string& diagnostic,
    ControlCodecError& error) {
    std::uint64_t value = 0;
    if (!read_optional_u64(object, name, value, present, diagnostic, error)) {
        return false;
    }
    if (!present) {
        return true;
    }
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        error = ControlCodecError::InvalidFieldType;
        diagnostic = std::string("field exceeds uint32 range: ") + name;
        return false;
    }

    out = static_cast<std::uint32_t>(value);
    return true;
}

bool read_required_u32(
    const Json& object,
    const char* name,
    std::uint32_t& out,
    std::string& diagnostic,
    ControlCodecError& error) {
    bool present = false;
    if (!read_optional_u32(object, name, out, present, diagnostic, error)) {
        return false;
    }
    if (!present) {
        error = ControlCodecError::MissingRequiredField;
        diagnostic = std::string("missing required field: ") + name;
        return false;
    }
    return true;
}

bool read_audio_format(
    const Json& object,
    AudioFormat& out,
    std::string& diagnostic,
    ControlCodecError& error) {
    if (!object.is_object()) {
        error = ControlCodecError::InvalidFieldType;
        diagnostic = "audio format must be an object";
        return false;
    }

    if (!read_required_string(object, "sample_format", out.sample_format, diagnostic, error)) {
        return false;
    }
    if (!read_required_u32(object, "sample_rate", out.sample_rate, diagnostic, error)) {
        return false;
    }
    if (!read_required_u32(object, "channels", out.channels, diagnostic, error)) {
        return false;
    }

    error = validate_audio_format(out, "audio_format", diagnostic);
    if (error != ControlCodecError::None) {
        return false;
    }

    return true;
}

bool read_optional_audio_format(
    const Json& object,
    const char* name,
    AudioFormat& out,
    std::string& diagnostic,
    ControlCodecError& error) {
    const Json* value = find_field(object, name);
    if (value == nullptr) {
        return true;
    }

    return read_audio_format(*value, out, diagnostic, error);
}

bool read_required_audio_format(
    const Json& object,
    const char* name,
    AudioFormat& out,
    std::string& diagnostic,
    ControlCodecError& error) {
    const Json* value = find_field(object, name);
    if (value == nullptr) {
        error = ControlCodecError::MissingRequiredField;
        diagnostic = std::string("missing required field: ") + name;
        return false;
    }

    return read_audio_format(*value, out, diagnostic, error);
}

bool read_capabilities(
    const Json& object,
    WorkerCapabilities& out,
    std::string& diagnostic,
    ControlCodecError& error) {
    const Json* value = find_field(object, "capabilities");
    if (value == nullptr) {
        error = ControlCodecError::MissingRequiredField;
        diagnostic = "missing required field: capabilities";
        return false;
    }
    if (!value->is_object()) {
        error = ControlCodecError::InvalidFieldType;
        diagnostic = "field must be an object: capabilities";
        return false;
    }

    if (!read_required_bool(*value, "streaming", out.streaming, diagnostic, error)) {
        return false;
    }
    if (!read_required_bool(*value, "cancellation", out.cancellation, diagnostic, error)) {
        return false;
    }
    if (!read_required_bool(*value, "instructions", out.instructions, diagnostic, error)) {
        return false;
    }
    if (!read_required_bool(*value, "voice_clone", out.voice_clone, diagnostic, error)) {
        return false;
    }

    return true;
}

Json audio_format_to_json(const AudioFormat& format) {
    return Json{
        {"sample_format", format.sample_format},
        {"sample_rate", format.sample_rate},
        {"channels", format.channels}
    };
}

Json capabilities_to_json(const WorkerCapabilities& capabilities) {
    return Json{
        {"streaming", capabilities.streaming},
        {"cancellation", capabilities.cancellation},
        {"instructions", capabilities.instructions},
        {"voice_clone", capabilities.voice_clone}
    };
}

JsonPayloadEncodeResult encode_json_payload(const Json& value) {
    try {
        JsonPayloadEncodeResult result;
        result.payload = string_to_bytes(value.dump());
        return result;
    }
    catch (const std::exception& exc) {
        return encode_error(ControlCodecError::EncodeFailed, exc.what());
    }
    catch (...) {
        return encode_error(ControlCodecError::EncodeFailed, "unknown JSON encode error");
    }
}

} // namespace qwen_tts_bridge::control_detail
