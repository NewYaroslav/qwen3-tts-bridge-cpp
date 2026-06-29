#include <qwen_tts_bridge/protocol/framing.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
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

using namespace qwen_tts_bridge;

std::vector<std::byte> bytes_from_string(const std::string& value) {
    std::vector<std::byte> bytes;
    bytes.reserve(value.size());
    for (const char ch : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return bytes;
}

std::vector<std::byte> make_payload(std::size_t size, unsigned char value = 0x5a) {
    return std::vector<std::byte>(size, static_cast<std::byte>(value));
}

std::vector<std::byte> require_encoded(const EncodeResult& result) {
    CHECK(static_cast<bool>(result));
    return result.bytes;
}

std::string string_from_bytes(const std::vector<std::byte>& bytes) {
    std::string value;
    value.reserve(bytes.size());
    for (const auto byte : bytes) {
        value.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return value;
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
    const auto encoded = require_encoded(encode_frame(FrameType::ControlJson, 0, payload));

    FrameParser parser;
    parser.append(encoded);

    const ParseResult result = parser.parse_next();

    CHECK(result.status == ParseStatus::FrameReady);
    CHECK(result.error == ProtocolError::None);
    CHECK(result.frame.header.protocol_version == ProtocolLimits::protocol_version);
    CHECK(result.frame.header.header_size == ProtocolLimits::min_header_size);
    CHECK(result.frame.header.frame_type == FrameType::ControlJson);
    CHECK(result.frame.header.flags == 0);
    CHECK(result.frame.header.payload_size == payload.size());
    CHECK(result.frame.header.request_id == 0);
    CHECK(string_from_bytes(result.frame.payload) == string_from_bytes(payload));
    CHECK(parser.buffered_size() == 0);
}

void test_fragmented_frame() {
    const auto payload = bytes_from_string("{\"message_type\":\"cancel\"}");
    const auto encoded = require_encoded(encode_frame(FrameType::ControlJson, 42, payload));

    FrameParser parser;
    parser.append(encoded.data(), 7);

    ParseResult result = parser.parse_next();
    CHECK(result.status == ParseStatus::NeedMoreData);

    parser.append(encoded.data() + 7, encoded.size() - 7);
    result = parser.parse_next();

    CHECK(result.status == ParseStatus::FrameReady);
    CHECK(result.frame.header.request_id == 42);
    CHECK(string_from_bytes(result.frame.payload) == string_from_bytes(payload));
}

void test_multiple_frames_in_one_read() {
    const auto first_payload = bytes_from_string("{\"message_type\":\"ping\",\"sequence\":1}");
    const auto second_payload = bytes_from_string("{\"message_type\":\"ping\",\"sequence\":2}");
    auto combined = require_encoded(encode_frame(FrameType::ControlJson, 0, first_payload));
    const auto second = require_encoded(encode_frame(FrameType::ControlJson, 0, second_payload));
    combined.insert(combined.end(), second.begin(), second.end());

    FrameParser parser;
    parser.append(combined);

    ParseResult result = parser.parse_next();
    CHECK(result.status == ParseStatus::FrameReady);
    CHECK(string_from_bytes(result.frame.payload) == string_from_bytes(first_payload));

    result = parser.parse_next();
    CHECK(result.status == ParseStatus::FrameReady);
    CHECK(string_from_bytes(result.frame.payload) == string_from_bytes(second_payload));

    result = parser.parse_next();
    CHECK(result.status == ParseStatus::NeedMoreData);
}

void test_trailing_partial_frame_is_preserved() {
    const auto first_payload = bytes_from_string("{\"message_type\":\"pong\",\"sequence\":1}");
    const auto second_payload = bytes_from_string("{\"message_type\":\"pong\",\"sequence\":2}");
    auto combined = require_encoded(encode_frame(FrameType::ControlJson, 0, first_payload));
    const auto second = require_encoded(encode_frame(FrameType::ControlJson, 0, second_payload));
    combined.insert(combined.end(), second.begin(), second.begin() + 10);

    FrameParser parser;
    parser.append(combined);

    ParseResult result = parser.parse_next();
    CHECK(result.status == ParseStatus::FrameReady);
    CHECK(string_from_bytes(result.frame.payload) == string_from_bytes(first_payload));
    CHECK(parser.buffered_size() == 10);

    result = parser.parse_next();
    CHECK(result.status == ParseStatus::NeedMoreData);

    parser.append(second.data() + 10, second.size() - 10);
    result = parser.parse_next();
    CHECK(result.status == ParseStatus::FrameReady);
    CHECK(string_from_bytes(result.frame.payload) == string_from_bytes(second_payload));
}

void test_extended_header_is_skipped() {
    const auto payload = bytes_from_string("{\"message_type\":\"pong\",\"sequence\":1}");
    auto encoded = require_encoded(encode_frame(FrameType::ControlJson, 0, payload));
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
    CHECK(result.status == ParseStatus::FrameReady);
    CHECK(result.frame.header.header_size == 28);
    CHECK(string_from_bytes(result.frame.payload) == string_from_bytes(payload));
}

void test_partial_extended_header_waits_for_more_data() {
    const auto payload = bytes_from_string("{\"message_type\":\"pong\",\"sequence\":1}");
    auto encoded = require_encoded(encode_frame(FrameType::ControlJson, 0, payload));
    write_u16_le(encoded, 6, 28);
    encoded.insert(encoded.begin() + 24, {
        static_cast<std::byte>(0xaa),
        static_cast<std::byte>(0xbb),
        static_cast<std::byte>(0xcc),
        static_cast<std::byte>(0xdd)
    });

    FrameParser parser;
    parser.append(encoded.data(), 26);

    ParseResult result = parser.parse_next();
    CHECK(result.status == ParseStatus::NeedMoreData);

    parser.append(encoded.data() + 26, encoded.size() - 26);
    result = parser.parse_next();
    CHECK(result.status == ParseStatus::FrameReady);
    CHECK(string_from_bytes(result.frame.payload) == string_from_bytes(payload));
}

void test_partial_payload_waits_for_more_data() {
    const auto payload = bytes_from_string("{\"message_type\":\"pong\",\"sequence\":1}");
    const auto encoded = require_encoded(encode_frame(FrameType::ControlJson, 0, payload));
    const std::size_t first_part_size = ProtocolLimits::min_header_size + 3;

    FrameParser parser;
    parser.append(encoded.data(), first_part_size);

    ParseResult result = parser.parse_next();
    CHECK(result.status == ParseStatus::NeedMoreData);

    parser.append(encoded.data() + first_part_size, encoded.size() - first_part_size);
    result = parser.parse_next();
    CHECK(result.status == ParseStatus::FrameReady);
    CHECK(string_from_bytes(result.frame.payload) == string_from_bytes(payload));
}

void test_reject_bad_magic() {
    auto encoded = require_encoded(encode_frame(FrameType::ControlJson, 0, bytes_from_string("{}")));
    encoded[0] = static_cast<std::byte>('X');

    FrameParser parser;
    parser.append(encoded);

    const ParseResult result = parser.parse_next();
    CHECK(result.status == ParseStatus::FatalError);
    CHECK(result.error == ProtocolError::InvalidMagic);
}

void test_reject_unsupported_version() {
    auto encoded = require_encoded(encode_frame(FrameType::ControlJson, 0, bytes_from_string("{}")));
    write_u16_le(encoded, 4, 2);

    FrameParser parser;
    parser.append(encoded);

    const ParseResult result = parser.parse_next();
    CHECK(result.status == ParseStatus::FatalError);
    CHECK(result.error == ProtocolError::UnsupportedProtocolVersion);
}

void test_reject_header_too_small() {
    auto encoded = require_encoded(encode_frame(FrameType::ControlJson, 0, bytes_from_string("{}")));
    write_u16_le(encoded, 6, 23);

    FrameParser parser;
    parser.append(encoded);

    const ParseResult result = parser.parse_next();
    CHECK(result.status == ParseStatus::FatalError);
    CHECK(result.error == ProtocolError::HeaderTooSmall);
}

void test_reject_header_too_large() {
    auto encoded = require_encoded(encode_frame(FrameType::ControlJson, 0, bytes_from_string("{}")));
    write_u16_le(encoded, 6, ProtocolLimits::max_header_size + 1);

    FrameParser parser;
    parser.append(encoded);

    const ParseResult result = parser.parse_next();
    CHECK(result.status == ParseStatus::FatalError);
    CHECK(result.error == ProtocolError::HeaderTooLarge);
}

void test_reject_unknown_frame_type() {
    auto encoded = require_encoded(encode_frame(FrameType::ControlJson, 0, bytes_from_string("{}")));
    write_u16_le(encoded, 8, 99);

    FrameParser parser;
    parser.append(encoded);

    const ParseResult result = parser.parse_next();
    CHECK(result.status == ParseStatus::FatalError);
    CHECK(result.error == ProtocolError::UnknownFrameType);
}

void test_reject_non_zero_flags() {
    auto encoded = require_encoded(encode_frame(FrameType::ControlJson, 0, bytes_from_string("{}")));
    write_u16_le(encoded, 10, 1);

    FrameParser parser;
    parser.append(encoded);

    const ParseResult result = parser.parse_next();
    CHECK(result.status == ParseStatus::FatalError);
    CHECK(result.error == ProtocolError::UnsupportedFlags);
}

void test_reject_oversized_control_payload_before_allocation() {
    auto encoded = require_encoded(encode_frame(FrameType::ControlJson, 0, bytes_from_string("{}")));
    write_u32_le(encoded, 12, ProtocolLimits::max_control_payload_bytes + 1);

    FrameParser parser;
    parser.append(encoded.data(), ProtocolLimits::min_header_size);

    const ParseResult result = parser.parse_next();
    CHECK(result.status == ParseStatus::FatalError);
    CHECK(result.error == ProtocolError::PayloadTooLarge);
}

void test_audio_payload_at_limit_is_allowed() {
    const auto payload = make_payload(ProtocolLimits::max_audio_payload_bytes);
    const auto encoded = require_encoded(encode_frame(FrameType::AudioPcm, 7, payload));

    FrameParser parser;
    parser.append(encoded);

    const ParseResult result = parser.parse_next();
    CHECK(result.status == ParseStatus::FrameReady);
    CHECK(result.frame.header.frame_type == FrameType::AudioPcm);
    CHECK(result.frame.header.payload_size == payload.size());
    CHECK(result.frame.payload.size() == payload.size());
}

void test_reject_audio_payload_above_limit_before_allocation() {
    auto encoded = require_encoded(encode_frame(FrameType::AudioPcm, 7, bytes_from_string("pcm")));
    write_u32_le(encoded, 12, ProtocolLimits::max_audio_payload_bytes + 1);

    FrameParser parser;
    parser.append(encoded.data(), ProtocolLimits::min_header_size);

    const ParseResult result = parser.parse_next();
    CHECK(result.status == ParseStatus::FatalError);
    CHECK(result.error == ProtocolError::PayloadTooLarge);
}

void test_reject_error_payload_above_limit_before_allocation() {
    auto encoded = require_encoded(encode_frame(FrameType::ErrorJson, 0, bytes_from_string("{}")));
    write_u32_le(encoded, 12, ProtocolLimits::max_error_payload_bytes + 1);

    FrameParser parser;
    parser.append(encoded.data(), ProtocolLimits::min_header_size);

    const ParseResult result = parser.parse_next();
    CHECK(result.status == ParseStatus::FatalError);
    CHECK(result.error == ProtocolError::PayloadTooLarge);
}

void test_empty_control_and_error_payloads_are_framing_valid() {
    const auto control = require_encoded(encode_frame(FrameType::ControlJson, 0, std::vector<std::byte>{}));
    const auto error = require_encoded(encode_frame(FrameType::ErrorJson, 0, std::vector<std::byte>{}));

    FrameParser parser;
    parser.append(control);
    parser.append(error);

    ParseResult result = parser.parse_next();
    CHECK(result.status == ParseStatus::FrameReady);
    CHECK(result.frame.header.frame_type == FrameType::ControlJson);
    CHECK(result.frame.payload.empty());

    result = parser.parse_next();
    CHECK(result.status == ParseStatus::FrameReady);
    CHECK(result.frame.header.frame_type == FrameType::ErrorJson);
    CHECK(result.frame.payload.empty());
}

void test_fatal_parser_stays_fatal_until_clear() {
    auto bad = require_encoded(encode_frame(FrameType::ControlJson, 0, bytes_from_string("{}")));
    bad[0] = static_cast<std::byte>('X');
    const auto good = require_encoded(encode_frame(FrameType::ControlJson, 0, bytes_from_string("{}")));

    FrameParser parser;
    parser.append(bad);

    ParseResult result = parser.parse_next();
    CHECK(result.status == ParseStatus::FatalError);
    CHECK(result.error == ProtocolError::InvalidMagic);

    parser.append(good);
    result = parser.parse_next();
    CHECK(result.status == ParseStatus::FatalError);
    CHECK(result.error == ProtocolError::InvalidMagic);

    parser.clear();
    parser.append(good);
    result = parser.parse_next();
    CHECK(result.status == ParseStatus::FrameReady);
}

void test_encoder_rejects_oversized_payload() {
    const auto payload = make_payload(ProtocolLimits::max_control_payload_bytes + 1u);
    const EncodeResult result = encode_frame(FrameType::ControlJson, 0, payload);

    CHECK(!result);
    CHECK(result.error == EncodeError::PayloadTooLarge);
    CHECK(result.bytes.empty());
}

void test_encoder_rejects_explicit_extended_header() {
    const auto payload = bytes_from_string("{}");
    FrameHeader header;
    header.header_size = ProtocolLimits::min_header_size + 4;
    header.payload_size = static_cast<std::uint32_t>(payload.size());

    const EncodeResult result = encode_frame(header, payload);

    CHECK(!result);
    CHECK(result.error == EncodeError::UnsupportedHeaderSize);
}

void test_encoder_rejects_payload_size_mismatch() {
    const auto payload = bytes_from_string("{}");
    FrameHeader header;
    header.payload_size = 0;

    const EncodeResult result = encode_frame(header, payload);

    CHECK(!result);
    CHECK(result.error == EncodeError::PayloadSizeMismatch);
}

} // namespace

int main() {
    test_round_trip_control_frame();
    test_fragmented_frame();
    test_multiple_frames_in_one_read();
    test_trailing_partial_frame_is_preserved();
    test_extended_header_is_skipped();
    test_partial_extended_header_waits_for_more_data();
    test_partial_payload_waits_for_more_data();
    test_reject_bad_magic();
    test_reject_unsupported_version();
    test_reject_header_too_small();
    test_reject_header_too_large();
    test_reject_unknown_frame_type();
    test_reject_non_zero_flags();
    test_reject_oversized_control_payload_before_allocation();
    test_audio_payload_at_limit_is_allowed();
    test_reject_audio_payload_above_limit_before_allocation();
    test_reject_error_payload_above_limit_before_allocation();
    test_empty_control_and_error_payloads_are_framing_valid();
    test_fatal_parser_stays_fatal_until_clear();
    test_encoder_rejects_oversized_payload();
    test_encoder_rejects_explicit_extended_header();
    test_encoder_rejects_payload_size_mismatch();
    return 0;
}
