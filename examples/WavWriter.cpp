#include "WavWriter.hpp"

#include <array>
#include <limits>
#include <stdexcept>
#include <utility>

namespace qwen_tts_bridge::examples {
namespace {

constexpr std::uint32_t kPcmFormat = 1;
constexpr std::uint32_t kFmtChunkSize = 16;
constexpr std::uint32_t kWavHeaderSize = 44;
constexpr std::uint32_t kRiffSizeWithoutData = 36;

[[nodiscard]] bool is_supported_bits_per_sample(std::uint16_t value) {
    return value == 8 || value == 16 || value == 24 || value == 32;
}

void write_bytes(std::ostream& output, const char* bytes, std::size_t size) {
    output.write(bytes, static_cast<std::streamsize>(size));
}

void write_u16_le(std::ostream& output, std::uint16_t value) {
    const std::array<char, 2> bytes{
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8u) & 0xFFu)
    };
    write_bytes(output, bytes.data(), bytes.size());
}

void write_u32_le(std::ostream& output, std::uint32_t value) {
    const std::array<char, 4> bytes{
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8u) & 0xFFu),
        static_cast<char>((value >> 16u) & 0xFFu),
        static_cast<char>((value >> 24u) & 0xFFu)
    };
    write_bytes(output, bytes.data(), bytes.size());
}

[[nodiscard]] std::uint32_t checked_u32(std::uint64_t value, const char* name) {
    if (value > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error(std::string(name) + " exceeds uint32 range");
    }
    return static_cast<std::uint32_t>(value);
}

} // namespace

WavWriter::WavWriter(
    std::string path,
    std::uint32_t sample_rate,
    std::uint16_t channels,
    std::uint16_t bits_per_sample)
    : path_(std::move(path)),
      sample_rate_(sample_rate),
      channels_(channels),
      bits_per_sample_(bits_per_sample) {
    if (path_.empty()) {
        throw std::runtime_error("output WAV path is empty");
    }
    if (sample_rate_ == 0) {
        throw std::runtime_error("sample rate must be greater than zero");
    }
    if (channels_ == 0) {
        throw std::runtime_error("channel count must be greater than zero");
    }
    if (!is_supported_bits_per_sample(bits_per_sample_)) {
        throw std::runtime_error("bits per sample must be 8, 16, 24, or 32");
    }

    const std::uint32_t block_align =
        static_cast<std::uint32_t>(channels_) * (bits_per_sample_ / 8u);
    if (block_align == 0 ||
        block_align > std::numeric_limits<std::uint16_t>::max()) {
        throw std::runtime_error("WAV block alignment exceeds uint16 range");
    }
    block_align_ = static_cast<std::uint16_t>(block_align);

    file_.open(path_, std::ios::binary | std::ios::trunc);
    if (!file_.is_open()) {
        throw std::runtime_error("failed to open WAV file: " + path_);
    }

    closed_ = false;
    write_header(0);
    ensure_stream_ok("write WAV header");
}

WavWriter::~WavWriter() noexcept {
    try {
        close();
    }
    catch (...) {
    }
}

void WavWriter::write_pcm(const std::byte* data, std::size_t size) {
    if (closed_) {
        throw std::runtime_error("cannot write PCM to a closed WAV file");
    }
    if (data == nullptr && size != 0) {
        throw std::runtime_error("PCM data pointer is null");
    }
    if (size == 0) {
        return;
    }
    if (size % block_align_ != 0) {
        throw std::runtime_error("PCM chunk does not contain complete sample frames");
    }

    constexpr std::uint64_t max_data_size =
        std::numeric_limits<std::uint32_t>::max() - kRiffSizeWithoutData;
    if (data_size_ > max_data_size ||
        static_cast<std::uint64_t>(size) > max_data_size - data_size_) {
        throw std::runtime_error("WAV file would exceed the RIFF 4 GiB limit");
    }

    file_.write(
        reinterpret_cast<const char*>(data),
        static_cast<std::streamsize>(size));
    ensure_stream_ok("write PCM data");
    data_size_ += static_cast<std::uint64_t>(size);
}

void WavWriter::close() {
    if (closed_) {
        return;
    }

    patch_header();
    file_.flush();
    ensure_stream_ok("flush WAV file");
    file_.close();
    closed_ = true;
}

std::uint64_t WavWriter::data_size() const noexcept {
    return data_size_;
}

void WavWriter::write_header(std::uint32_t data_size) {
    const std::uint32_t byte_rate =
        checked_u32(
            static_cast<std::uint64_t>(sample_rate_) * block_align_,
            "byte rate");
    const std::uint32_t riff_size =
        checked_u32(
            static_cast<std::uint64_t>(kRiffSizeWithoutData) + data_size,
            "RIFF chunk size");

    write_bytes(file_, "RIFF", 4);
    write_u32_le(file_, riff_size);
    write_bytes(file_, "WAVE", 4);
    write_bytes(file_, "fmt ", 4);
    write_u32_le(file_, kFmtChunkSize);
    write_u16_le(file_, static_cast<std::uint16_t>(kPcmFormat));
    write_u16_le(file_, channels_);
    write_u32_le(file_, sample_rate_);
    write_u32_le(file_, byte_rate);
    write_u16_le(file_, block_align_);
    write_u16_le(file_, bits_per_sample_);
    write_bytes(file_, "data", 4);
    write_u32_le(file_, data_size);

    static_assert(kWavHeaderSize == 44, "unexpected WAV header size");
}

void WavWriter::patch_header() {
    const std::uint32_t data_size = checked_u32(data_size_, "WAV data size");

    file_.seekp(4, std::ios::beg);
    write_u32_le(
        file_,
        checked_u32(
            static_cast<std::uint64_t>(kRiffSizeWithoutData) + data_size,
            "RIFF chunk size"));

    file_.seekp(40, std::ios::beg);
    write_u32_le(file_, data_size);
    file_.seekp(0, std::ios::end);
    ensure_stream_ok("patch WAV header");
}

void WavWriter::ensure_stream_ok(const char* action) {
    if (!file_) {
        throw std::runtime_error(std::string("failed to ") + action + ": " + path_);
    }
}

} // namespace qwen_tts_bridge::examples
