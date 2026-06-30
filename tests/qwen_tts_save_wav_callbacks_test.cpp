#include <qwen_tts_bridge/audio.hpp>
#include <qwen_tts_bridge/client/ClientTypes.hpp>

#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#define CHECK(expr)                                                            \
    do {                                                                       \
        if (!(expr)) {                                                         \
            std::cerr << "CHECK failed: " #expr << " at " << __FILE__ << ':'  \
                      << __LINE__ << '\n';                                     \
            std::exit(EXIT_FAILURE);                                           \
        }                                                                      \
    } while (false)

namespace {

using qwen_tts_bridge::AudioFormat;
using qwen_tts_bridge::PcmChunk;
using qwen_tts_bridge::audio::SaveWavState;
using qwen_tts_bridge::audio::WavWriter;
using qwen_tts_bridge::audio::make_save_wav_callbacks;
using qwen_tts_bridge::audio::wait_for_save_wav_terminal;

std::string output_path(const std::string& file_name) {
    return std::string(QWEN_TTS_BRIDGE_TEST_OUTPUT_DIR) + "/" + file_name;
}

std::vector<std::byte> pcm_bytes(std::size_t size) {
    return std::vector<std::byte>(size, std::byte{0});
}

void remove_file(const std::string& path) {
    std::remove(path.c_str());
}

void test_audio_format_mismatch_marks_terminal_error() {
    const std::string path = output_path("save_wav_callbacks_mismatch.wav");
    remove_file(path);

    SaveWavState state;
    AudioFormat expected;
    WavWriter writer(path, expected.sample_rate, 1, 16);
    auto callbacks = make_save_wav_callbacks(state, writer, expected);

    PcmChunk chunk;
    chunk.format = expected;
    chunk.format.sample_rate = expected.sample_rate * 2;
    chunk.bytes = pcm_bytes(2);

    callbacks.on_audio(chunk);

    CHECK(wait_for_save_wav_terminal(state, std::chrono::seconds(1)));

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        CHECK(state.terminal);
        CHECK(!state.success);
        CHECK(state.audio_chunks == 0);
        CHECK(state.audio_bytes == 0);
        CHECK(state.message.find("unexpected PCM format") != std::string::npos);
    }

    writer.close();
    remove_file(path);
}

void test_valid_audio_then_completed_marks_success() {
    const std::string path = output_path("save_wav_callbacks_success.wav");
    remove_file(path);

    SaveWavState state;
    AudioFormat expected;
    WavWriter writer(path, expected.sample_rate, 1, 16);
    auto callbacks = make_save_wav_callbacks(state, writer, expected);

    PcmChunk chunk;
    chunk.format = expected;
    chunk.bytes = pcm_bytes(4);

    callbacks.on_audio(chunk);
    callbacks.on_completed();

    CHECK(wait_for_save_wav_terminal(state, std::chrono::seconds(1)));

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        CHECK(state.terminal);
        CHECK(state.success);
        CHECK(state.message == "completed");
        CHECK(state.audio_chunks == 1);
        CHECK(state.audio_bytes == chunk.bytes.size());
    }
    CHECK(writer.data_size() == chunk.bytes.size());

    remove_file(path);
}

} // namespace

int main() {
    test_audio_format_mismatch_marks_terminal_error();
    test_valid_audio_then_completed_marks_success();
    return EXIT_SUCCESS;
}
