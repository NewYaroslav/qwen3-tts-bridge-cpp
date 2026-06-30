#include "ControlCodecInternal.hpp"

#include <exception>
#include <utility>

namespace qwen_tts_bridge::control_detail {

namespace {

ErrorDecodeResult decode_error_object(const Json& object) {
    if (!object.is_object()) {
        return error_error(
            ControlCodecError::PayloadNotObject,
            "error payload must be a JSON object");
    }

    if (has_forbidden_header_field(object)) {
        return error_error(
            ControlCodecError::ForbiddenField,
            "error payload must not contain protocol_version or request_id");
    }

    const Json* message_type_value = find_field(object, kMessageType);
    if (message_type_value == nullptr) {
        return error_error(ControlCodecError::MissingMessageType, "missing message_type");
    }
    if (!message_type_value->is_string()) {
        return error_error(ControlCodecError::InvalidMessageType, "message_type must be a string");
    }
    if (message_type_value->get<std::string>() != "error") {
        return error_error(ControlCodecError::UnknownMessageType, "error_json must use message_type=error");
    }

    ErrorMessage message;
    std::string diagnostic;
    ControlCodecError error = ControlCodecError::None;
    if (!read_required_string(object, "category", message.category, diagnostic, error) ||
        !read_required_string(object, "code", message.code, diagnostic, error) ||
        !read_required_string(object, "message", message.message, diagnostic, error)) {
        return error_error(error, diagnostic);
    }
    error = validate_error_message(message, diagnostic);
    if (error != ControlCodecError::None) {
        return error_error(error, diagnostic);
    }

    ErrorDecodeResult result;
    result.message = std::move(message);
    return result;
}

} // namespace

Json error_message_to_json(const ErrorMessage& message) {
    return Json{
        {kMessageType, "error"},
        {"category", message.category},
        {"code", message.code},
        {"message", message.message}
    };
}

ErrorDecodeResult decode_error_payload(
    const std::byte* data,
    std::size_t size) {
    if (data == nullptr && size != 0) {
        return error_error(ControlCodecError::InvalidJson, "payload data is null");
    }

    try {
        const auto payload = bytes_to_string(data, size);
        const Json value = Json::parse(payload);
        return decode_error_object(value);
    }
    catch (const Json::parse_error& exc) {
        return error_error(ControlCodecError::InvalidJson, exc.what());
    }
    catch (const Json::type_error& exc) {
        return error_error(ControlCodecError::InvalidFieldType, exc.what());
    }
    catch (const std::exception& exc) {
        return error_error(ControlCodecError::InvalidJson, exc.what());
    }
    catch (...) {
        return error_error(
            ControlCodecError::InvalidJson,
            "unknown JSON decode error");
    }
}

} // namespace qwen_tts_bridge::control_detail
