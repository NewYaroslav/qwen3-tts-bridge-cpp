#include <qwen_tts_bridge/protocol/framing.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using namespace qwen_tts_bridge;

std::vector<std::byte> bytes_from_string(const std::string& value) {
    std::vector<std::byte> bytes;
    bytes.reserve(value.size());
    for (const char ch : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return bytes;
}

std::string string_from_bytes(const std::vector<std::byte>& bytes) {
    std::string value;
    value.reserve(bytes.size());
    for (const auto byte : bytes) {
        value.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return value;
}

std::uint16_t read_u16_le(const std::vector<std::byte>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset])) |
        (static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset + 1])) << 8));
}

void write_u16_le(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::byte>((value >> 8) & 0xffu);
}

void write_u32_le(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::byte>(value & 0xffu);
    bytes[offset + 1] = static_cast<std::byte>((value >> 8) & 0xffu);
    bytes[offset + 2] = static_cast<std::byte>((value >> 16) & 0xffu);
    bytes[offset + 3] = static_cast<std::byte>((value >> 24) & 0xffu);
}

void test_round_trip_control_frame() {
    const auto payload = bytes_from_string("{\"message_type\":\"ping\",\"sequence\":1}");
    const auto encoded = encode_frame(FrameType::ControlJson, 0, payload);

    FrameParser parser;
    parser.append(encoded);

    const ParseResult result = parser.parse_next();

    assert(result.status == ParseStatus::FrameReady);
    assert(result.error == ProtocolError::None);
    assert(result.frame.header.protocol_version == ProtocolLimits::protocol_version);
    assert(result.frame.header.header_size == ProtocolLimits::min_header_size);
    assert(result.frame.header.frame_type == FrameType::ControlJson);
    assert(result.frame.header.flags == 0);
    assert(result.frame.header.payload_size == payload.size());
    assert(result.frame.header.request_id == 0);
    assert(string_from_bytes(result.frame.payload) == string_from_bytes(payload));
    assert(parser.buffered_size() == 0);
}

void test_fragmented_frame() {
    const auto payload = bytes_from_string("{\"message_type\":\"cancel\"}");
    const auto encoded = encode_frame(FrameType::ControlJson, 42, payload);

    FrameParser parser;
    parser.append(encoded.data(), 7);

    ParseResult result = parser.parse_next();
    assert(result.status == ParseStatus::NeedMoreData);

    parser.append(encoded.data() + 7, encoded.size() - 7);
    result = parser.parse_next();

    assert(result.status == ParseStatus::FrameReady);
    assert(result.frame.header.request_id == 42);
    assert(string_from_bytes(result.frame.payload) == string_from_bytes(payload));
}

void test_multiple_frames_in_one_read() {
    const auto first_payload = bytes_from_string("{\"message_type\":\"ping\",\"sequence\":1}");
    const auto second_payload = bytes_from_string("{\"message_type\":\"ping\",\"sequence\":2}");
    auto combined = encode_frame(FrameType::ControlJson, 0, first_payload);
    const auto second = encode_frame(FrameType::ControlJson, 0, second_payload);
    combined.insert(combined.end(), second.begin(), second.end());

    FrameParser parser;
    parser.append(combined);

    ParseResult result = parser.parse_next();
    assert(result.status == ParseStatus::FrameReady);
    assert(string_from_bytes(result.frame.payload) == string_from_bytes(first_payload));

    result = parser.parse_next();
    assert(result.status == ParseStatus::FrameReady);
    assert(string_from_bytes(result.frame.payload) == string_from_bytes(second_payload));

    result = parser.parse_next();
    assert(result.status == ParseStatus::NeedMoreData);
}

void test_extended_header_is_skipped() {
    const auto payload = bytes_from_string("{\"message_type\":\"pong\",\"sequence\":1}");
    auto encoded = encode_frame(FrameType::ControlJson, 0, payload);
    write_u16_le(encoded, 6, 28);
    encoded.insert(encoded.begin() + 24, {
        static_cast<std::byte>(0xaa),
        static_cast<std::byte>(0xbb),
        static_cast<std::byte>(0xcc),
        static_cast<std::byte>(0xdd)
    });

    FrameParser parser;
    parser.append(encoded);

    const ParseResult result = parser.parse_next();
    assert(result.status == ParseStatus::FrameReady);
    assert(result.frame.header.header_size == 28);
    assert(string_from_bytes(result.frame.payload) == string_from_bytes(payload));
}

void test_reject_bad_magic() {
    const auto payload = bytes_from_string("{}");
    auto encoded = encode_frame(FrameType::ControlJson, 0, payload);
    encoded[0] = static_cast<std::byte>('X');

    FrameParser parser;
    parser.append(encoded);

    const ParseResult result = parser.parse_next();
    assert(result.status == ParseStatus::FatalError);
    assert(result.error == ProtocolError::InvalidMagic);
}

void test_reject_unsupported_version() {
    const auto payload = bytes_from_string("{}");
    auto encoded = encode_frame(FrameType::ControlJson, 0, payload);
    write_u16_le(encoded, 4, 2);

    FrameParser parser;
    parser.append(encoded);

    const ParseResult result = parser.parse_next();
    assert(result.status == ParseStatus::FatalError);
    assert(result.error == ProtocolError::UnsupportedProtocolVersion);
}

void test_reject_header_too_small() {
    const auto payload = bytes_from_string("{}");
    auto encoded = encode_frame(FrameType::ControlJson, 0, payload);
    write_u16_le(encoded, 6, 23);

    FrameParser parser;
    parser.append(encoded);

    const ParseResult result = parser.parse_next();
    assert(result.status == ParseStatus::FatalError);
    assert(result.error == ProtocolError::HeaderTooSmall);
}

void test_reject_unknown_frame_type() {
    const auto payload = bytes_from_string("{}");
    auto encoded = encode_frame(FrameType::ControlJson, 0, payload);
    write_u16_le(encoded, 8, 99);

    FrameParser parser;
    parser.append(encoded);

    const ParseResult result = parser.parse_next();
    assert(result.status == ParseStatus::FatalError);
    assert(result.error == ProtocolError::UnknownFrameType);
}

void test_reject_non_zero_flags() {
    const auto payload = bytes_from_string("{}");
    auto encoded = encode_frame(FrameType::ControlJson, 0, payload);
    write_u16_le(encoded, 10, 1);

    FrameParser parser;
    parser.append(encoded);

    const ParseResult result = parser.parse_next();
    assert(result.status == ParseStatus::FatalError);
    assert(result.error == ProtocolError::UnsupportedFlags);
}

void test_reject_oversized_control_payload_before_allocation() {
    const auto payload = bytes_from_string("{}");
    auto encoded = encode_frame(FrameType::ControlJson, 0, payload);
    write_u32_le(encoded, 12, ProtocolLimits::max_control_payload_bytes + 1);

    FrameParser parser;
    parser.append(encoded.data(), ProtocolLimits::min_header_size);

    const ParseResult result = parser.parse_next();
    assert(result.status == ParseStatus::FatalError);
    assert(result.error == ProtocolError::PayloadTooLarge);
}

} // namespace

int main() {
    test_round_trip_control_frame();
    test_fragmented_frame();
    test_multiple_frames_in_one_read();
    test_extended_header_is_skipped();
    test_reject_bad_magic();
    test_reject_unsupported_version();
    test_reject_header_too_small();
    test_reject_unknown_frame_type();
    test_reject_non_zero_flags();
    test_reject_oversized_control_payload_before_allocation();
    return 0;
}
