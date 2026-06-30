#pragma once

/// \file SaveWavCallbacks.hpp
/// \brief Callback helpers shared by the save-wav example and its tests.

#include <qwen_tts_bridge/audio/WavWriter.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

#include <qwen_tts_bridge/client/ClientTypes.hpp>

namespace qwen_tts_bridge::audio {

/// \struct SaveWavState
/// \brief Shared completion state for the async save-wav example.
struct SaveWavState {
    std::mutex mutex;
    std::mutex writer_mutex;
    std::condition_variable condition;
    bool terminal = false;
    bool success = false;
    std::string message;
    std::size_t audio_chunks = 0;
    std::uint64_t audio_bytes = 0;
};

/// \brief Marks the example request as terminal and wakes waiters.
void mark_save_wav_finished(
    SaveWavState& state,
    bool success,
    std::string message);

/// \brief Waits until the request reaches a terminal callback state.
bool wait_for_save_wav_terminal(
    SaveWavState& state,
    std::chrono::milliseconds timeout);

/// \brief Builds callbacks that stream matching PCM chunks into a WAV writer.
TtsCallbacks make_save_wav_callbacks(
    SaveWavState& state,
    WavWriter& writer,
    const AudioFormat& expected_format);

} // namespace qwen_tts_bridge::audio
