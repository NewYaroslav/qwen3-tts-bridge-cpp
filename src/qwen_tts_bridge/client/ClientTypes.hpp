#pragma once

/// \file ClientTypes.hpp
/// \brief Public request, callback, and error DTOs for QwenTtsClient.

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace qwen_tts_bridge {

/// \struct TtsRequest
/// \brief User-facing synthesis request.
struct TtsRequest {
    /// \brief Optional request identifier. Zero asks the client to assign one.
    RequestId id = 0;

    /// \brief Spoken UTF-8 text.
    std::string text;

    /// \brief Natural-language or engine language name.
    std::string language = "auto";

    /// \brief Optional worker speaker identifier or voice name.
    ///
    /// Empty means no explicit speaker was selected by the application. Some
    /// engines may choose a default voice, while Qwen CustomVoice models may
    /// require a concrete speaker name.
    std::string speaker;

    /// \brief Natural-language style, emotion, or prosody instruction.
    std::string instruction;

    /// \brief Requested PCM output format.
    AudioFormat output;
};

/// \struct PcmChunk
/// \brief User-facing PCM audio chunk routed to a request callback.
struct PcmChunk {
    /// \brief Request that produced this audio.
    RequestId request_id = 0;

    /// \brief PCM format for the chunk.
    AudioFormat format;

    /// \brief Raw PCM bytes.
    std::vector<std::byte> bytes;
};

/// \struct TtsError
/// \brief User-facing worker, protocol, transport, or session error.
struct TtsError {
    /// \brief Related request, or zero for session-level failures.
    RequestId request_id = 0;

    /// \brief Error category.
    std::string category;

    /// \brief Stable error code within the category when available.
    std::string code;

    /// \brief Human-readable diagnostic message.
    std::string message;
};

/// \struct TtsCallbacks
/// \brief Callback set for one synthesis request.
struct TtsCallbacks {
    /// \brief Called for each PCM chunk.
    std::function<void(const PcmChunk&)> on_audio;

    /// \brief Called exactly once when synthesis completes successfully.
    std::function<void()> on_completed;

    /// \brief Called exactly once when synthesis is cancelled.
    std::function<void()> on_cancelled;

    /// \brief Called exactly once when synthesis fails.
    std::function<void(const TtsError&)> on_error;
};

} // namespace qwen_tts_bridge
