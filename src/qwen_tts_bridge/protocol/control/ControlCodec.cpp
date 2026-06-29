#include <qwen_tts_bridge/protocol/control/ControlCodec.hpp>
#include <qwen_tts_bridge/protocol/control/ControlCodecInternal.hpp>

#include <string>
#include <type_traits>
#include <variant>

namespace qwen_tts_bridge {

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
    return control_detail::decode_control_payload(data, size, direction);
}

[[nodiscard]] ControlDecodeResult decode_control_message(
    const std::vector<std::byte>& payload,
    ControlMessageDirection direction) {
    return decode_control_message(payload.data(), payload.size(), direction);
}

[[nodiscard]] JsonPayloadEncodeResult encode_control_message(
    const ControlMessage& message) {
    std::string diagnostic;
    const auto error = control_detail::validate_control_message(message, diagnostic);
    if (error != ControlCodecError::None) {
        return control_detail::encode_error(error, diagnostic);
    }
    return control_detail::encode_json_payload(
        control_detail::control_message_to_json(message));
}

[[nodiscard]] ErrorDecodeResult decode_error_message(
    const std::byte* data,
    std::size_t size) {
    return control_detail::decode_error_payload(data, size);
}

[[nodiscard]] ErrorDecodeResult decode_error_message(
    const std::vector<std::byte>& payload) {
    return decode_error_message(payload.data(), payload.size());
}

[[nodiscard]] JsonPayloadEncodeResult encode_error_message(
    const ErrorMessage& message) {
    std::string diagnostic;
    const auto error = control_detail::validate_error_message(message, diagnostic);
    if (error != ControlCodecError::None) {
        return control_detail::encode_error(error, diagnostic);
    }
    return control_detail::encode_json_payload(
        control_detail::error_message_to_json(message));
}

} // namespace qwen_tts_bridge
