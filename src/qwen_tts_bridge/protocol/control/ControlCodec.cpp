#include <qwen_tts_bridge/protocol/control/ControlCodec.hpp>

#include <nlohmann/json.hpp>

#include <limits>
#include <type_traits>
#include <utility>

namespace qwen_tts_bridge {
namespace {

using Json = nlohmann::json;

constexpr const char* kMessageType = "message_type";
constexpr const char* kProtocolVersion = "protocol_version";
constexpr const char* kRequestId = "request_id";

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

bool json_to_u64(
    const Json& value,
    std::uint64_t& out) {
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

ControlCodecError validate_non_empty_string(
    const std::string& value,
    const char* name,
    std::string& diagnostic) {
    if (!value.empty()) {
        return ControlCodecError::None;
    }

    diagnostic = std::string("field must not be empty: ") + name;
    return ControlCodecError::InvalidFieldType;
}

ControlCodecError validate_audio_format(
    const AudioFormat& format,
    const char* name,
    std::string& diagnostic) {
    const std::string prefix = std::string(name) + '.';
    const std::string sample_format_name = prefix + "sample_format";
    if (const auto error = validate_non_empty_string(
            format.sample_format,
            sample_format_name.c_str(),
            diagnostic);
        error != ControlCodecError::None) {
        return error;
    }
    if (format.sample_rate == 0) {
        diagnostic = prefix + "sample_rate must be greater than zero";
        return ControlCodecError::InvalidFieldType;
    }
    if (format.channels == 0) {
        diagnostic = prefix + "channels must be greater than zero";
        return ControlCodecError::InvalidFieldType;
    }

    return ControlCodecError::None;
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
ControlCodecError validate_control_payload(
    const Message& value,
    std::string& diagnostic) {
    if constexpr (std::is_same_v<Message, HelloMessage>) {
        if (const auto error = validate_non_empty_string(
                value.client_name,
                "client_name",
                diagnostic);
            error != ControlCodecError::None) {
            return error;
        }
        return validate_non_empty_string(
            value.client_version,
            "client_version",
            diagnostic);
    }
    else if constexpr (std::is_same_v<Message, SynthesizeMessage>) {
        if (const auto error = validate_non_empty_string(value.text, "text", diagnostic);
            error != ControlCodecError::None) {
            return error;
        }
        return validate_audio_format(value.output, "output", diagnostic);
    }
    else if constexpr (std::is_same_v<Message, ShutdownMessage>) {
        if (value.mode == "cancel") {
            return ControlCodecError::None;
        }
        diagnostic = "unsupported shutdown mode";
        return ControlCodecError::InvalidFieldType;
    }
    else if constexpr (std::is_same_v<Message, ReadyMessage>) {
        if (const auto error = validate_non_empty_string(
                value.worker_version,
                "worker_version",
                diagnostic);
            error != ControlCodecError::None) {
            return error;
        }
        return validate_non_empty_string(value.session_id, "session_id", diagnostic);
    }
    else if constexpr (std::is_same_v<Message, QueuedMessage>) {
        if (!value.has_position || value.position > 0) {
            return ControlCodecError::None;
        }
        diagnostic = "position must be greater than zero";
        return ControlCodecError::InvalidFieldType;
    }
    else if constexpr (std::is_same_v<Message, StartedMessage>) {
        return validate_audio_format(value.audio_format, "audio_format", diagnostic);
    }
    else {
        return ControlCodecError::None;
    }
}

ControlCodecError validate_control_message(
    const ControlMessage& message,
    std::string& diagnostic) {
    return std::visit(
        [&diagnostic](const auto& value) {
            return validate_control_payload(value, diagnostic);
        },
        message);
}

ControlCodecError validate_error_message(
    const ErrorMessage& message,
    std::string& diagnostic) {
    if (const auto error = validate_non_empty_string(
            message.category,
            "category",
            diagnostic);
        error != ControlCodecError::None) {
        return error;
    }
    if (const auto error = validate_non_empty_string(message.code, "code", diagnostic);
        error != ControlCodecError::None) {
        return error;
    }
    return validate_non_empty_string(message.message, "message", diagnostic);
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
        error = validate_control_payload(message, diagnostic);
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
        error = validate_control_payload(message, diagnostic);
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
        error = validate_control_payload(message, diagnostic);
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
        error = validate_control_payload(message, diagnostic);
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
        error = validate_control_payload(message, diagnostic);
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

Json control_message_to_json(const ControlMessage& message) {
    return std::visit(
        [](const auto& value) -> Json {
            using Message = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<Message, HelloMessage>) {
                return Json{
                    {kMessageType, "hello"},
                    {"client_name", value.client_name},
                    {"client_version", value.client_version}
                };
            }
            else if constexpr (std::is_same_v<Message, SynthesizeMessage>) {
                return Json{
                    {kMessageType, "synthesize"},
                    {"text", value.text},
                    {"language", value.language},
                    {"speaker", value.speaker},
                    {"instruction", value.instruction},
                    {"output", audio_format_to_json(value.output)}
                };
            }
            else if constexpr (std::is_same_v<Message, CancelMessage>) {
                return Json{{kMessageType, "cancel"}};
            }
            else if constexpr (std::is_same_v<Message, PingMessage>) {
                Json out = {{kMessageType, "ping"}};
                if (value.has_sequence) {
                    out["sequence"] = value.sequence;
                }
                return out;
            }
            else if constexpr (std::is_same_v<Message, ShutdownMessage>) {
                return Json{
                    {kMessageType, "shutdown"},
                    {"mode", value.mode}
                };
            }
            else if constexpr (std::is_same_v<Message, ReadyMessage>) {
                Json out = {
                    {kMessageType, "ready"},
                    {"worker_version", value.worker_version},
                    {"session_id", value.session_id},
                    {"capabilities", capabilities_to_json(value.capabilities)}
                };
                if (value.has_warmed_up) {
                    out["warmed_up"] = value.warmed_up;
                }
                return out;
            }
            else if constexpr (std::is_same_v<Message, QueuedMessage>) {
                Json out = {{kMessageType, "queued"}};
                if (value.has_position) {
                    out["position"] = value.position;
                }
                return out;
            }
            else if constexpr (std::is_same_v<Message, StartedMessage>) {
                return Json{
                    {kMessageType, "started"},
                    {"audio_format", audio_format_to_json(value.audio_format)}
                };
            }
            else if constexpr (std::is_same_v<Message, CompletedMessage>) {
                return Json{{kMessageType, "completed"}};
            }
            else if constexpr (std::is_same_v<Message, CancelledMessage>) {
                return Json{{kMessageType, "cancelled"}};
            }
            else if constexpr (std::is_same_v<Message, PongMessage>) {
                Json out = {{kMessageType, "pong"}};
                if (value.has_sequence) {
                    out["sequence"] = value.sequence;
                }
                return out;
            }
            else {
                return Json{{kMessageType, "shutdown_ack"}};
            }
        },
        message);
}

Json error_message_to_json(const ErrorMessage& message) {
    return Json{
        {kMessageType, "error"},
        {"category", message.category},
        {"code", message.code},
        {"message", message.message}
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

template <typename Result, typename Callback>
Result parse_json_payload(
    const std::byte* data,
    std::size_t size,
    Callback callback) {
    if (data == nullptr && size != 0) {
        Result result;
        result.error = ControlCodecError::InvalidJson;
        result.diagnostic = "payload data is null";
        return result;
    }

    try {
        const auto payload = bytes_to_string(data, size);
        const Json value = Json::parse(payload);
        return callback(value);
    }
    catch (const Json::parse_error& exc) {
        Result result;
        result.error = ControlCodecError::InvalidJson;
        result.diagnostic = exc.what();
        return result;
    }
    catch (const Json::type_error& exc) {
        Result result;
        result.error = ControlCodecError::InvalidFieldType;
        result.diagnostic = exc.what();
        return result;
    }
    catch (const std::exception& exc) {
        Result result;
        result.error = ControlCodecError::InvalidJson;
        result.diagnostic = exc.what();
        return result;
    }
    catch (...) {
        Result result;
        result.error = ControlCodecError::InvalidJson;
        result.diagnostic = "unknown JSON decode error";
        return result;
    }
}

} // namespace

const char* control_codec_error_code(ControlCodecError error) {
    switch (error) {
    case ControlCodecError::None:
        return "";
    case ControlCodecError::InvalidJson:
        return "invalid_json";
    case ControlCodecError::PayloadNotObject:
        return "payload_not_object";
    case ControlCodecError::MissingMessageType:
        return "missing_message_type";
    case ControlCodecError::InvalidMessageType:
        return "invalid_message_type";
    case ControlCodecError::UnknownMessageType:
        return "unknown_message_type";
    case ControlCodecError::InvalidMessageDirection:
        return "invalid_message_direction";
    case ControlCodecError::MissingRequiredField:
        return "missing_required_field";
    case ControlCodecError::InvalidFieldType:
        return "invalid_field_type";
    case ControlCodecError::ForbiddenField:
        return "invalid_field_type";
    case ControlCodecError::EncodeFailed:
        return "internal_error";
    }

    return "internal_error";
}

ControlMessageType control_message_type(const ControlMessage& message) {
    return std::visit(
        [](const auto& value) {
            using Message = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<Message, HelloMessage>) {
                return ControlMessageType::Hello;
            }
            else if constexpr (std::is_same_v<Message, SynthesizeMessage>) {
                return ControlMessageType::Synthesize;
            }
            else if constexpr (std::is_same_v<Message, CancelMessage>) {
                return ControlMessageType::Cancel;
            }
            else if constexpr (std::is_same_v<Message, PingMessage>) {
                return ControlMessageType::Ping;
            }
            else if constexpr (std::is_same_v<Message, ShutdownMessage>) {
                return ControlMessageType::Shutdown;
            }
            else if constexpr (std::is_same_v<Message, ReadyMessage>) {
                return ControlMessageType::Ready;
            }
            else if constexpr (std::is_same_v<Message, QueuedMessage>) {
                return ControlMessageType::Queued;
            }
            else if constexpr (std::is_same_v<Message, StartedMessage>) {
                return ControlMessageType::Started;
            }
            else if constexpr (std::is_same_v<Message, CompletedMessage>) {
                return ControlMessageType::Completed;
            }
            else if constexpr (std::is_same_v<Message, CancelledMessage>) {
                return ControlMessageType::Cancelled;
            }
            else if constexpr (std::is_same_v<Message, PongMessage>) {
                return ControlMessageType::Pong;
            }
            else {
                return ControlMessageType::ShutdownAck;
            }
        },
        message);
}

[[nodiscard]] ControlDecodeResult decode_control_message(
    const std::byte* data,
    std::size_t size,
    ControlMessageDirection direction) {
    return parse_json_payload<ControlDecodeResult>(
        data,
        size,
        [direction](const Json& value) {
            return decode_control_object(value, direction);
        });
}

[[nodiscard]] ControlDecodeResult decode_control_message(
    const std::vector<std::byte>& payload,
    ControlMessageDirection direction) {
    return decode_control_message(payload.data(), payload.size(), direction);
}

[[nodiscard]] JsonPayloadEncodeResult encode_control_message(
    const ControlMessage& message) {
    std::string diagnostic;
    const auto error = validate_control_message(message, diagnostic);
    if (error != ControlCodecError::None) {
        return encode_error(error, diagnostic);
    }
    return encode_json_payload(control_message_to_json(message));
}

[[nodiscard]] ErrorDecodeResult decode_error_message(
    const std::byte* data,
    std::size_t size) {
    return parse_json_payload<ErrorDecodeResult>(
        data,
        size,
        [](const Json& value) {
            return decode_error_object(value);
        });
}

[[nodiscard]] ErrorDecodeResult decode_error_message(
    const std::vector<std::byte>& payload) {
    return decode_error_message(payload.data(), payload.size());
}

[[nodiscard]] JsonPayloadEncodeResult encode_error_message(
    const ErrorMessage& message) {
    std::string diagnostic;
    const auto error = validate_error_message(message, diagnostic);
    if (error != ControlCodecError::None) {
        return encode_error(error, diagnostic);
    }
    return encode_json_payload(error_message_to_json(message));
}

} // namespace qwen_tts_bridge
