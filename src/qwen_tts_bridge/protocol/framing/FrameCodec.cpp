#include <qwen_tts_bridge/protocol/framing/FrameCodec.hpp>

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

std::vector<std::byte> encode_frame(
    FrameType frame_type,
    RequestId request_id,
    const std::vector<std::byte>& payload) {
    FrameHeader header;
    header.frame_type = frame_type;
    header.request_id = request_id;
    header.payload_size = static_cast<std::uint32_t>(payload.size());
    return encode_frame(header, payload);
}

std::vector<std::byte> encode_frame(
    const FrameHeader& header,
    const std::vector<std::byte>& payload) {
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
    return out;
}

} // namespace qwen_tts_bridge
