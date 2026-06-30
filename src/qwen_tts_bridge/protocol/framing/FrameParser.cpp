#include <qwen_tts_bridge/protocol/framing.hpp>

#include <utility>

namespace qwen_tts_bridge {
namespace {

constexpr unsigned char kMagic[4] = {'Q', 'T', 'B', '1'};

std::uint16_t read_u16_le(const std::byte* data) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(std::to_integer<unsigned char>(data[0])) |
        (static_cast<std::uint16_t>(std::to_integer<unsigned char>(data[1])) << 8));
}

std::uint32_t read_u32_le(const std::byte* data) {
    return static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[0])) |
        (static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[1])) << 8) |
        (static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[2])) << 16) |
        (static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[3])) << 24));
}

std::uint64_t read_u64_le(const std::byte* data) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(std::to_integer<unsigned char>(data[i])) << (i * 8);
    }
    return value;
}

ParseResult fatal(ProtocolError error, const std::string& message) {
    ParseResult result;
    result.status = ParseStatus::FatalError;
    result.error = error;
    result.message = message;
    return result;
}

FrameType to_frame_type(std::uint16_t value) {
    return static_cast<FrameType>(value);
}

bool has_magic(const std::byte* data) {
    for (std::size_t i = 0; i < 4; ++i) {
        if (std::to_integer<unsigned char>(data[i]) != kMagic[i]) {
            return false;
        }
    }
    return true;
}

} // namespace

void FrameParser::append(const std::byte* data, std::size_t size) {
    if (fatal_ || data == nullptr || size == 0) {
        return;
    }

    buffer_.insert(buffer_.end(), data, data + size);
}

void FrameParser::append(const std::vector<std::byte>& data) {
    append(data.data(), data.size());
}

ParseResult FrameParser::parse_next() {
    if (fatal_) {
        return fatal(fatal_error_, fatal_message_);
    }

    if (buffer_.size() - read_offset_ < ProtocolLimits::min_header_size) {
        return ParseResult();
    }

    const auto* data = buffer_.data() + read_offset_;

    if (!has_magic(data)) {
        return make_fatal(ProtocolError::InvalidMagic, "invalid frame magic");
    }

    const std::uint16_t protocol_version = read_u16_le(data + 4);
    const std::uint16_t header_size = read_u16_le(data + 6);
    const std::uint16_t frame_type_value = read_u16_le(data + 8);
    const std::uint16_t flags = read_u16_le(data + 10);
    const std::uint32_t payload_size = read_u32_le(data + 12);
    const RequestId request_id = read_u64_le(data + 16);

    if (protocol_version != ProtocolLimits::protocol_version) {
        return make_fatal(ProtocolError::UnsupportedProtocolVersion, "unsupported protocol version");
    }

    if (header_size < ProtocolLimits::min_header_size) {
        return make_fatal(ProtocolError::HeaderTooSmall, "header size is smaller than v1 minimum");
    }

    if (header_size > ProtocolLimits::max_header_size) {
        return make_fatal(ProtocolError::HeaderTooLarge, "header size exceeds v1 maximum");
    }

    if (!is_known_frame_type(frame_type_value)) {
        return make_fatal(ProtocolError::UnknownFrameType, "unknown frame type");
    }

    if (flags != 0) {
        return make_fatal(ProtocolError::UnsupportedFlags, "unsupported frame flags");
    }

    const FrameType frame_type = to_frame_type(frame_type_value);
    if (payload_size > ProtocolLimits::max_frame_payload_bytes ||
        payload_size > max_payload_size(frame_type)) {
        return make_fatal(ProtocolError::PayloadTooLarge, "payload size exceeds v1 limit");
    }

    const std::size_t total_size = static_cast<std::size_t>(header_size) +
        static_cast<std::size_t>(payload_size);
    if (total_size < header_size) {
        return make_fatal(ProtocolError::PayloadTooLarge, "frame size overflow");
    }

    if (buffer_.size() - read_offset_ < total_size) {
        return ParseResult();
    }

    Frame frame;
    frame.header.protocol_version = protocol_version;
    frame.header.header_size = header_size;
    frame.header.frame_type = frame_type;
    frame.header.flags = flags;
    frame.header.payload_size = payload_size;
    frame.header.request_id = request_id;
    frame.payload.assign(
        buffer_.begin() + static_cast<std::ptrdiff_t>(read_offset_ + header_size),
        buffer_.begin() + static_cast<std::ptrdiff_t>(read_offset_ + total_size));

    read_offset_ += total_size;
    compact_buffer();

    ParseResult result;
    result.status = ParseStatus::FrameReady;
    result.frame = std::move(frame);
    return result;
}

std::size_t FrameParser::buffered_size() const {
    return buffer_.size() - read_offset_;
}

void FrameParser::clear() {
    buffer_.clear();
    read_offset_ = 0;
    fatal_ = false;
    fatal_error_ = ProtocolError::None;
    fatal_message_.clear();
}

void FrameParser::compact_buffer() {
    constexpr std::size_t compact_threshold = 64u * 1024u;

    if (read_offset_ == 0) {
        return;
    }

    if (read_offset_ == buffer_.size()) {
        buffer_.clear();
        read_offset_ = 0;
        return;
    }

    if (read_offset_ >= compact_threshold || read_offset_ > buffer_.size() / 2u) {
        buffer_.erase(
            buffer_.begin(),
            buffer_.begin() + static_cast<std::ptrdiff_t>(read_offset_));
        read_offset_ = 0;
    }
}

ParseResult FrameParser::make_fatal(ProtocolError error, const std::string& message) {
    fatal_ = true;
    fatal_error_ = error;
    fatal_message_ = message;
    return fatal(error, message);
}

} // namespace qwen_tts_bridge
