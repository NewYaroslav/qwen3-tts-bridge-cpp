#include "ControlCodecInternal.hpp"

#include <type_traits>
#include <utility>

namespace qwen_tts_bridge::control_detail {

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

} // namespace qwen_tts_bridge::control_detail
