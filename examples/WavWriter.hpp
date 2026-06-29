#pragma once

/// \file WavWriter.hpp
/// \brief Small RIFF/WAVE PCM writer used by the save-wav example.

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>

namespace qwen_tts_bridge::examples {

/// \class WavWriter
/// \brief Writes raw PCM bytes into a RIFF/WAVE file and patches the header on close.
///
/// The writer owns the output file. It is intentionally small and supports the
/// uncompressed PCM formats needed by the bridge examples.
class WavWriter final {
public:
    /// \brief Opens a WAV file and writes a placeholder header.
    /// \param path Output file path.
    /// \param sample_rate PCM sample rate in Hz.
    /// \param channels Number of PCM channels.
    /// \param bits_per_sample Bits per sample, for example 16 for s16le.
    WavWriter(
        std::string path,
        std::uint32_t sample_rate,
        std::uint16_t channels,
        std::uint16_t bits_per_sample);

    /// \brief Finalizes the file if it is still open.
    ~WavWriter() noexcept;

    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    /// \brief Writes one PCM byte chunk.
    /// \param data PCM bytes.
    /// \param size Byte count.
    void write_pcm(const std::byte* data, std::size_t size);

    /// \brief Patches RIFF sizes and closes the file.
    void close();

    /// \brief Returns the number of PCM bytes written so far.
    [[nodiscard]] std::uint64_t data_size() const noexcept;

private:
    void write_header(std::uint32_t data_size);
    void patch_header();
    void ensure_stream_ok(const char* action);

    std::ofstream file_;
    std::string path_;
    std::uint32_t sample_rate_ = 0;
    std::uint16_t channels_ = 0;
    std::uint16_t bits_per_sample_ = 0;
    std::uint16_t block_align_ = 0;
    std::uint64_t data_size_ = 0;
    bool closed_ = true;
};

} // namespace qwen_tts_bridge::examples
