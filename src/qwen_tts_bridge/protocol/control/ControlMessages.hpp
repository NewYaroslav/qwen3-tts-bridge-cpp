#pragma once

/// \file ControlMessages.hpp
/// \brief DTOs for protocol v1 JSON control and error payloads.

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace qwen_tts_bridge {

/// \enum ControlMessageDirection
/// \brief Expected wire direction for a control_json payload.
enum class ControlMessageDirection {
    ClientToWorker, ///< Payload sent by the C++ client to the worker.
    WorkerToClient  ///< Payload sent by the worker to the C++ client.
};

/// \enum ControlMessageType
/// \brief Known protocol v1 control message names.
enum class ControlMessageType {
    Hello,
    Synthesize,
    Cancel,
    Ping,
    Shutdown,
    Ready,
    Queued,
    Started,
    Completed,
    Cancelled,
    Pong,
    ShutdownAck
};

/// \enum ControlCodecError
/// \brief JSON-level codec errors for control and error payloads.
enum class ControlCodecError {
    None,
    InvalidJson,
    PayloadNotObject,
    MissingMessageType,
    InvalidMessageType,
    UnknownMessageType,
    InvalidMessageDirection,
    MissingRequiredField,
    InvalidFieldType,
    ForbiddenField,
    EncodeFailed
};

/// \brief Returns the protocol error code string for a codec error.
/// \param error Codec error category.
/// \return Stable protocol error code string.
const char* control_codec_error_code(ControlCodecError error);

/// \struct AudioFormat
/// \brief PCM format announced or requested by control messages.
struct AudioFormat {
    std::string sample_format = "s16le";
    std::uint32_t sample_rate = 24000;
    std::uint32_t channels = 1;
};

/// \struct WorkerCapabilities
/// \brief Worker feature flags announced by the ready message.
struct WorkerCapabilities {
    bool streaming = false;
    bool cancellation = false;
    bool instructions = false;
    bool voice_clone = false;
};

/// \struct HelloMessage
/// \brief Client-to-worker session handshake message.
struct HelloMessage {
    std::string client_name;
    std::string client_version;
};

/// \struct SynthesizeMessage
/// \brief Client-to-worker synthesis request payload.
struct SynthesizeMessage {
    std::string text;
    std::string language = "auto";
    std::string speaker;
    std::string instruction;
    AudioFormat output;
};

/// \struct CancelMessage
/// \brief Client-to-worker request cancellation payload.
struct CancelMessage {};

/// \struct PingMessage
/// \brief Client-to-worker heartbeat payload.
struct PingMessage {
    bool has_sequence = false;
    std::uint64_t sequence = 0;
};

/// \struct ShutdownMessage
/// \brief Client-to-worker graceful shutdown payload.
struct ShutdownMessage {
    std::string mode = "cancel";
};

/// \struct ReadyMessage
/// \brief Worker-to-client readiness handshake payload.
struct ReadyMessage {
    std::string worker_version;
    std::string session_id;
    bool has_warmed_up = false;
    bool warmed_up = false;
    WorkerCapabilities capabilities;
};

/// \struct QueuedMessage
/// \brief Worker-to-client advisory request queue event.
struct QueuedMessage {
    bool has_position = false;
    std::uint32_t position = 0;
};

/// \struct StartedMessage
/// \brief Worker-to-client request start event.
struct StartedMessage {
    AudioFormat audio_format;
};

/// \struct CompletedMessage
/// \brief Worker-to-client terminal success event.
struct CompletedMessage {};

/// \struct CancelledMessage
/// \brief Worker-to-client terminal cancellation event.
struct CancelledMessage {};

/// \struct PongMessage
/// \brief Worker-to-client heartbeat response payload.
struct PongMessage {
    bool has_sequence = false;
    std::uint64_t sequence = 0;
};

/// \struct ShutdownAckMessage
/// \brief Worker-to-client graceful shutdown acknowledgement.
struct ShutdownAckMessage {};

/// \brief Variant containing one known control_json message payload.
using ControlMessage = std::variant<
    HelloMessage,
    SynthesizeMessage,
    CancelMessage,
    PingMessage,
    ShutdownMessage,
    ReadyMessage,
    QueuedMessage,
    StartedMessage,
    CompletedMessage,
    CancelledMessage,
    PongMessage,
    ShutdownAckMessage>;

/// \brief Returns the message type represented by a control message variant.
/// \param message Control message payload.
/// \return Message type discriminator.
ControlMessageType control_message_type(const ControlMessage& message);

/// \struct ErrorMessage
/// \brief Worker-to-client error_json payload.
///
/// Category and code are kept as strings for forward-compatible protocol
/// extensions. The control codec validates that they are present and non-empty,
/// but it does not enforce a closed set of known wire values.
struct ErrorMessage {
    std::string category;
    std::string code;
    std::string message;
};

/// \struct ControlDecodeResult
/// \brief Result returned when decoding a control_json payload.
struct [[nodiscard]] ControlDecodeResult {
    ControlMessage message;
    ControlCodecError error = ControlCodecError::None;
    std::string diagnostic;

    /// \brief Returns true when decoding succeeded.
    explicit operator bool() const noexcept {
        return error == ControlCodecError::None;
    }
};

/// \struct ErrorDecodeResult
/// \brief Result returned when decoding an error_json payload.
struct [[nodiscard]] ErrorDecodeResult {
    ErrorMessage message;
    ControlCodecError error = ControlCodecError::None;
    std::string diagnostic;

    /// \brief Returns true when decoding succeeded.
    explicit operator bool() const noexcept {
        return error == ControlCodecError::None;
    }
};

/// \struct JsonPayloadEncodeResult
/// \brief Result returned when encoding a JSON payload.
struct [[nodiscard]] JsonPayloadEncodeResult {
    std::vector<std::byte> payload;
    ControlCodecError error = ControlCodecError::None;
    std::string diagnostic;

    /// \brief Returns true when encoding succeeded.
    explicit operator bool() const noexcept {
        return error == ControlCodecError::None;
    }
};

} // namespace qwen_tts_bridge
