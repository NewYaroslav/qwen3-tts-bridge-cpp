#include "SaveWavCallbacks.hpp"

#include <stdexcept>
#include <utility>

namespace qwen_tts_bridge::examples {
namespace {

bool is_terminal(SaveWavState& state) {
    std::lock_guard<std::mutex> lock(state.mutex);
    return state.terminal;
}

bool matches_format(const PcmChunk& chunk, const AudioFormat& expected) {
    return chunk.format.sample_format == expected.sample_format &&
           chunk.format.sample_rate == expected.sample_rate &&
           chunk.format.channels == expected.channels;
}

void close_writer_ignoring_errors(
    SaveWavState& state,
    WavWriter& writer) noexcept {
    try {
        std::lock_guard<std::mutex> writer_lock(state.writer_mutex);
        writer.close();
    }
    catch (...) {
    }
}

} // namespace

void mark_save_wav_finished(
    SaveWavState& state,
    bool success,
    std::string message) {
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if (state.terminal) {
            return;
        }
        state.terminal = true;
        state.success = success;
        state.message = std::move(message);
    }
    state.condition.notify_all();
}

bool wait_for_save_wav_terminal(
    SaveWavState& state,
    std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(state.mutex);
    if (timeout.count() == 0) {
        state.condition.wait(lock, [&state]() {
            return state.terminal;
        });
        return true;
    }

    return state.condition.wait_for(lock, timeout, [&state]() {
        return state.terminal;
    });
}

TtsCallbacks make_save_wav_callbacks(
    SaveWavState& state,
    WavWriter& writer,
    const AudioFormat& expected_format) {
    TtsCallbacks callbacks;

    callbacks.on_audio = [&state, &writer, expected_format](const PcmChunk& chunk) {
        if (is_terminal(state)) {
            return;
        }

        try {
            if (!matches_format(chunk, expected_format)) {
                throw std::runtime_error("worker produced an unexpected PCM format");
            }

            {
                std::lock_guard<std::mutex> writer_lock(state.writer_mutex);
                if (is_terminal(state)) {
                    return;
                }
                writer.write_pcm(chunk.bytes.data(), chunk.bytes.size());
            }

            {
                std::lock_guard<std::mutex> state_lock(state.mutex);
                if (!state.terminal) {
                    ++state.audio_chunks;
                    state.audio_bytes += chunk.bytes.size();
                }
            }
        }
        catch (const std::exception& exc) {
            mark_save_wav_finished(state, false, exc.what());
        }
        catch (...) {
            mark_save_wav_finished(state, false, "unknown audio callback failure");
        }
    };

    callbacks.on_completed = [&state, &writer]() {
        if (is_terminal(state)) {
            return;
        }

        try {
            {
                std::lock_guard<std::mutex> writer_lock(state.writer_mutex);
                writer.close();
            }
            mark_save_wav_finished(state, true, "completed");
        }
        catch (const std::exception& exc) {
            mark_save_wav_finished(state, false, exc.what());
        }
        catch (...) {
            mark_save_wav_finished(state, false, "unknown completion callback failure");
        }
    };

    callbacks.on_cancelled = [&state, &writer]() {
        close_writer_ignoring_errors(state, writer);
        mark_save_wav_finished(state, false, "request was cancelled");
    };

    callbacks.on_error = [&state, &writer](const TtsError& error) {
        close_writer_ignoring_errors(state, writer);
        mark_save_wav_finished(
            state,
            false,
            error.category + "/" + error.code + ": " + error.message);
    };

    return callbacks;
}

} // namespace qwen_tts_bridge::examples
