#include "ControlCodecInternal.hpp"

#include <exception>
#include <utility>

namespace qwen_tts_bridge::control_detail {

namespace {

bool is_client_to_worker_message(const std::string& type) {
    return type == "hello" ||
           type == "synthesize" ||
           type == "cancel" ||
           type == "ping" ||
           type == "shutdown";
}

bool is_worker_to_client_message(const std::string& type) {
    return type == "ready" ||
           type == "queued" ||
           type == "started" ||
           type == "completed" ||
           type == "cancelled" ||
           type == "pong" ||
           type == "shutdown_ack";
}

bool is_known_control_message(const std::string& type) {
    return is_client_to_worker_message(type) || is_worker_to_client_message(type);
}

bool direction_allows(
    const std::string& type,
    ControlMessageDirection direction) {
    if (direction == ControlMessageDirection::ClientToWorker) {
        return is_client_to_worker_message(type);
    }
    return is_worker_to_client_message(type);
}

template <typename Message>
ControlDecodeResult make_control_message(Message message) {
    ControlDecodeResult result;
    result.message = std::move(message);
    return result;
}

ControlDecodeResult decode_known_control_message(
    const Json& object,
    const std::string& type) {
    std::string diagnostic;
    ControlCodecError error = ControlCodecError::None;

    if (type == "hello") {
        HelloMessage message;
        if (!read_required_string(object, "client_name", message.client_name, diagnostic, error) ||
            !read_required_string(object, "client_version", message.client_version, diagnostic, error)) {
            return control_error(error, diagnostic);
        }
        error = validate_control_message(ControlMessage{message}, diagnostic);
        if (error != ControlCodecError::None) {
            return control_error(error, diagnostic);
        }
        return make_control_message(std::move(message));
    }

    if (type == "synthesize") {
        SynthesizeMessage message;
        if (!read_required_string(object, "text", message.text, diagnostic, error) ||
            !read_optional_string(object, "language", message.language, diagnostic, error) ||
            !read_optional_string(object, "speaker", message.speaker, diagnostic, error) ||
            !read_optional_string(object, "instruction", message.instruction, diagnostic, error) ||
            !read_optional_audio_format(object, "output", message.output, diagnostic, error)) {
            return control_error(error, diagnostic);
        }
        error = validate_control_message(ControlMessage{message}, diagnostic);
        if (error != ControlCodecError::None) {
            return control_error(error, diagnostic);
        }
        return make_control_message(std::move(message));
    }

    if (type == "cancel") {
        return make_control_message(CancelMessage{});
    }

    if (type == "ping") {
        PingMessage message;
        if (!read_optional_u64(object, "sequence", message.sequence, message.has_sequence, diagnostic, error)) {
            return control_error(error, diagnostic);
        }
        return make_control_message(message);
    }

    if (type == "shutdown") {
        ShutdownMessage message;
        if (!read_optional_string(object, "mode", message.mode, diagnostic, error)) {
            return control_error(error, diagnostic);
        }
        if (message.mode != "cancel") {
            return control_error(
                ControlCodecError::InvalidFieldType,
                "unsupported shutdown mode");
        }
        return make_control_message(std::move(message));
    }

    if (type == "ready") {
        ReadyMessage message;
        if (!read_required_string(object, "worker_version", message.worker_version, diagnostic, error) ||
            !read_required_string(object, "session_id", message.session_id, diagnostic, error) ||
            !read_optional_bool(object, "warmed_up", message.warmed_up, message.has_warmed_up, diagnostic, error) ||
            !read_capabilities(object, message.capabilities, diagnostic, error)) {
            return control_error(error, diagnostic);
        }
        error = validate_control_message(ControlMessage{message}, diagnostic);
        if (error != ControlCodecError::None) {
            return control_error(error, diagnostic);
        }
        return make_control_message(std::move(message));
    }

    if (type == "queued") {
        QueuedMessage message;
        if (!read_optional_u32(object, "position", message.position, message.has_position, diagnostic, error)) {
            return control_error(error, diagnostic);
        }
        error = validate_control_message(ControlMessage{message}, diagnostic);
        if (error != ControlCodecError::None) {
            return control_error(error, diagnostic);
        }
        return make_control_message(message);
    }

    if (type == "started") {
        StartedMessage message;
        if (!read_required_audio_format(object, "audio_format", message.audio_format, diagnostic, error)) {
            return control_error(error, diagnostic);
        }
        error = validate_control_message(ControlMessage{message}, diagnostic);
        if (error != ControlCodecError::None) {
            return control_error(error, diagnostic);
        }
        return make_control_message(std::move(message));
    }

    if (type == "completed") {
        return make_control_message(CompletedMessage{});
    }

    if (type == "cancelled") {
        return make_control_message(CancelledMessage{});
    }

    if (type == "pong") {
        PongMessage message;
        if (!read_optional_u64(object, "sequence", message.sequence, message.has_sequence, diagnostic, error)) {
            return control_error(error, diagnostic);
        }
        return make_control_message(message);
    }

    if (type == "shutdown_ack") {
        return make_control_message(ShutdownAckMessage{});
    }

    return control_error(ControlCodecError::UnknownMessageType, "unknown message_type");
}

ControlDecodeResult decode_control_object(
    const Json& object,
    ControlMessageDirection direction) {
    if (!object.is_object()) {
        return control_error(
            ControlCodecError::PayloadNotObject,
            "control payload must be a JSON object");
    }

    if (has_forbidden_header_field(object)) {
        return control_error(
            ControlCodecError::ForbiddenField,
            "control payload must not contain protocol_version or request_id");
    }

    const Json* message_type_value = find_field(object, kMessageType);
    if (message_type_value == nullptr) {
        return control_error(
            ControlCodecError::MissingMessageType,
            "missing message_type");
    }
    if (!message_type_value->is_string()) {
        return control_error(
            ControlCodecError::InvalidMessageType,
            "message_type must be a string");
    }

    const std::string message_type = message_type_value->get<std::string>();
    if (!is_known_control_message(message_type)) {
        return control_error(
            ControlCodecError::UnknownMessageType,
            "unknown message_type: " + message_type);
    }
    if (!direction_allows(message_type, direction)) {
        return control_error(
            ControlCodecError::InvalidMessageDirection,
            "message_type is not valid for this direction: " + message_type);
    }

    return decode_known_control_message(object, message_type);
}

} // namespace

ControlDecodeResult decode_control_payload(
    const std::byte* data,
    std::size_t size,
    ControlMessageDirection direction) {
    if (data == nullptr && size != 0) {
        return control_error(ControlCodecError::InvalidJson, "payload data is null");
    }

    try {
        const auto payload = bytes_to_string(data, size);
        const Json value = Json::parse(payload);
        return decode_control_object(value, direction);
    }
    catch (const Json::parse_error& exc) {
        return control_error(ControlCodecError::InvalidJson, exc.what());
    }
    catch (const Json::type_error& exc) {
        return control_error(ControlCodecError::InvalidFieldType, exc.what());
    }
    catch (const std::exception& exc) {
        return control_error(ControlCodecError::InvalidJson, exc.what());
    }
    catch (...) {
        return control_error(
            ControlCodecError::InvalidJson,
            "unknown JSON decode error");
    }
}

} // namespace qwen_tts_bridge::control_detail
