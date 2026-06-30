#include <qwen_tts_bridge/protocol/framing.hpp>

#include <limits>
#include <utility>

namespace qwen_tts_bridge {
namespace {

constexpr unsigned char kMagic[4] = {'Q', 'T', 'B', '1'};

void write_u16_le(std::vector<std::byte>& out, std::uint16_t value) {
    out.push_back(static_cast<std::byte>(value & 0xffu));
    out.push_back(static_cast<std::byte>((value >> 8) & 0xffu));
}

void write_u32_le(std::vector<std::byte>& out, std::uint32_t value) {
    out.push_back(static_cast<std::byte>(value & 0xffu));
    out.push_back(static_cast<std::byte>((value >> 8) & 0xffu));
    out.push_back(static_cast<std::byte>((value >> 16) & 0xffu));
    out.push_back(static_cast<std::byte>((value >> 24) & 0xffu));
}

void write_u64_le(std::vector<std::byte>& out, std::uint64_t value) {
    for (std::size_t i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xffu));
    }
}

EncodeResult encode_error(EncodeError error, const std::string& message) {
    EncodeResult result;
    result.error = error;
    result.message = message;
    return result;
}

} // namespace

bool is_known_frame_type(std::uint16_t value) {
    return value == static_cast<std::uint16_t>(FrameType::ControlJson) ||
           value == static_cast<std::uint16_t>(FrameType::AudioPcm) ||
           value == static_cast<std::uint16_t>(FrameType::ErrorJson);
}

std::uint32_t max_payload_size(FrameType frame_type) {
    switch (frame_type) {
    case FrameType::ControlJson:
        return ProtocolLimits::max_control_payload_bytes;
    case FrameType::AudioPcm:
        return ProtocolLimits::max_audio_payload_bytes;
    case FrameType::ErrorJson:
        return ProtocolLimits::max_error_payload_bytes;
    }

    return 0;
}

[[nodiscard]] EncodeResult encode_frame(
    FrameType frame_type,
    RequestId request_id,
    const std::vector<std::byte>& payload) {
    FrameHeader header;
    header.frame_type = frame_type;
    header.request_id = request_id;
    header.payload_size = static_cast<std::uint32_t>(payload.size());
    return encode_frame(header, payload);
}

[[nodiscard]] EncodeResult encode_frame(
    const FrameHeader& header,
    const std::vector<std::byte>& payload) {
    if (header.protocol_version != ProtocolLimits::protocol_version) {
        return encode_error(
            EncodeError::UnsupportedProtocolVersion,
            "unsupported protocol version");
    }

    if (header.header_size != ProtocolLimits::min_header_size) {
        return encode_error(
            EncodeError::UnsupportedHeaderSize,
            "v1 encoder cannot emit header extensions");
    }

    const auto frame_type_value = static_cast<std::uint16_t>(header.frame_type);
    if (!is_known_frame_type(frame_type_value)) {
        return encode_error(EncodeError::UnknownFrameType, "unknown frame type");
    }

    if (header.flags != 0) {
        return encode_error(EncodeError::UnsupportedFlags, "unsupported frame flags");
    }

    if (payload.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return encode_error(
            EncodeError::PayloadTooLarge,
            "payload size exceeds uint32 limit");
    }

    if (header.payload_size != payload.size()) {
        return encode_error(
            EncodeError::PayloadSizeMismatch,
            "header payload_size does not match payload bytes");
    }

    if (payload.size() > ProtocolLimits::max_frame_payload_bytes ||
        payload.size() > max_payload_size(header.frame_type)) {
        return encode_error(
            EncodeError::PayloadTooLarge,
            "payload size exceeds v1 limit");
    }

    std::vector<std::byte> out;
    out.reserve(ProtocolLimits::min_header_size + payload.size());

    for (const auto byte : kMagic) {
        out.push_back(static_cast<std::byte>(byte));
    }

    write_u16_le(out, header.protocol_version);
    write_u16_le(out, header.header_size);
    write_u16_le(out, static_cast<std::uint16_t>(header.frame_type));
    write_u16_le(out, header.flags);
    write_u32_le(out, static_cast<std::uint32_t>(payload.size()));
    write_u64_le(out, header.request_id);

    out.insert(out.end(), payload.begin(), payload.end());

    EncodeResult result;
    result.bytes = std::move(out);
    return result;
}

} // namespace qwen_tts_bridge
