#include <qwen_tts_bridge/protocol/control.hpp>

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <variant>
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

std::string string_from_bytes(const std::vector<std::byte>& bytes) {
    std::string value;
    value.reserve(bytes.size());
    for (const auto byte : bytes) {
        value.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return value;
}

ControlDecodeResult decode_client(const std::string& payload) {
    return decode_control_message(
        bytes_from_string(payload),
        ControlMessageDirection::ClientToWorker);
}

ControlDecodeResult decode_worker(const std::string& payload) {
    return decode_control_message(
        bytes_from_string(payload),
        ControlMessageDirection::WorkerToClient);
}

void test_decode_hello() {
    const auto result = decode_client(
        "{\"message_type\":\"hello\","
        "\"client_name\":\"qwen-tts-bridge-cpp\","
        "\"client_version\":\"0.2.0\"}");

    CHECK(result);
    CHECK(control_message_type(result.message) == ControlMessageType::Hello);
    const auto& hello = std::get<HelloMessage>(result.message);
    CHECK(hello.client_name == "qwen-tts-bridge-cpp");
    CHECK(hello.client_version == "0.2.0");
}

void test_decode_synthesize_with_instruction_and_output() {
    const auto result = decode_client(
        "{\"message_type\":\"synthesize\","
        "\"text\":\"I thought you were not coming.\","
        "\"language\":\"English\","
        "\"speaker\":\"default\","
        "\"instruction\":\"Speak with relief.\","
        "\"output\":{"
        "\"sample_format\":\"s16le\","
        "\"sample_rate\":24000,"
        "\"channels\":1"
        "}}");

    CHECK(result);
    CHECK(control_message_type(result.message) == ControlMessageType::Synthesize);
    const auto& message = std::get<SynthesizeMessage>(result.message);
    CHECK(message.text == "I thought you were not coming.");
    CHECK(message.language == "English");
    CHECK(message.speaker == "default");
    CHECK(message.instruction == "Speak with relief.");
    CHECK(message.output.sample_format == "s16le");
    CHECK(message.output.sample_rate == 24000);
    CHECK(message.output.channels == 1);
}

void test_decode_ready() {
    const auto result = decode_worker(
        "{\"message_type\":\"ready\","
        "\"worker_version\":\"0.2.0\","
        "\"session_id\":\"session-1\","
        "\"warmed_up\":true,"
        "\"capabilities\":{"
        "\"streaming\":true,"
        "\"cancellation\":true,"
        "\"instructions\":true,"
        "\"voice_clone\":false"
        "}}");

    CHECK(result);
    CHECK(control_message_type(result.message) == ControlMessageType::Ready);
    const auto& ready = std::get<ReadyMessage>(result.message);
    CHECK(ready.worker_version == "0.2.0");
    CHECK(ready.session_id == "session-1");
    CHECK(ready.has_warmed_up);
    CHECK(ready.warmed_up);
    CHECK(ready.capabilities.streaming);
    CHECK(ready.capabilities.cancellation);
    CHECK(ready.capabilities.instructions);
    CHECK(!ready.capabilities.voice_clone);
}

void test_encode_ping_round_trip() {
    PingMessage ping;
    ping.has_sequence = true;
    ping.sequence = 17;

    const auto encoded = encode_control_message(ControlMessage{ping});
    CHECK(encoded);

    const std::string payload = string_from_bytes(encoded.payload);
    CHECK(payload.find("\"message_type\":\"ping\"") != std::string::npos);
    CHECK(payload.find("\"sequence\":17") != std::string::npos);
    CHECK(payload.find("request_id") == std::string::npos);
    CHECK(payload.find("protocol_version") == std::string::npos);

    const auto decoded = decode_control_message(
        encoded.payload,
        ControlMessageDirection::ClientToWorker);
    CHECK(decoded);
    const auto& decoded_ping = std::get<PingMessage>(decoded.message);
    CHECK(decoded_ping.has_sequence);
    CHECK(decoded_ping.sequence == 17);
}

void test_decode_started_audio_format() {
    const auto result = decode_worker(
        "{\"message_type\":\"started\","
        "\"audio_format\":{"
        "\"sample_format\":\"s16le\","
        "\"sample_rate\":24000,"
        "\"channels\":1"
        "}}");

    CHECK(result);
    CHECK(control_message_type(result.message) == ControlMessageType::Started);
    const auto& started = std::get<StartedMessage>(result.message);
    CHECK(started.audio_format.sample_format == "s16le");
    CHECK(started.audio_format.sample_rate == 24000);
    CHECK(started.audio_format.channels == 1);
}

void test_decode_error_json() {
    const auto result = decode_error_message(bytes_from_string(
        "{\"message_type\":\"error\","
        "\"category\":\"request_error\","
        "\"code\":\"unsupported_audio_format\","
        "\"message\":\"Unsupported format.\"}"));

    CHECK(result);
    CHECK(result.message.category == "request_error");
    CHECK(result.message.code == "unsupported_audio_format");
    CHECK(result.message.message == "Unsupported format.");
}

void test_encode_error_json() {
    ErrorMessage message;
    message.category = "model_error";
    message.code = "synthesis_failed";
    message.message = "Failure details.";

    const auto encoded = encode_error_message(message);
    CHECK(encoded);

    const auto decoded = decode_error_message(encoded.payload);
    CHECK(decoded);
    CHECK(decoded.message.category == message.category);
    CHECK(decoded.message.code == message.code);
    CHECK(decoded.message.message == message.message);
}

void test_reject_invalid_json() {
    const auto result = decode_client("{");
    CHECK(!result);
    CHECK(result.error == ControlCodecError::InvalidJson);
    CHECK(std::string(control_codec_error_code(result.error)) == "invalid_json");
}

void test_reject_non_object_payload() {
    const auto result = decode_client("[]");
    CHECK(!result);
    CHECK(result.error == ControlCodecError::PayloadNotObject);
    CHECK(std::string(control_codec_error_code(result.error)) == "payload_not_object");
}

void test_reject_missing_message_type() {
    const auto result = decode_client("{}");
    CHECK(!result);
    CHECK(result.error == ControlCodecError::MissingMessageType);
}

void test_reject_invalid_message_type() {
    const auto result = decode_client("{\"message_type\":42}");
    CHECK(!result);
    CHECK(result.error == ControlCodecError::InvalidMessageType);
}

void test_reject_unknown_message_type() {
    const auto result = decode_client("{\"message_type\":\"mystery\"}");
    CHECK(!result);
    CHECK(result.error == ControlCodecError::UnknownMessageType);
}

void test_reject_invalid_direction() {
    const auto result = decode_worker(
        "{\"message_type\":\"ping\",\"sequence\":17}");
    CHECK(!result);
    CHECK(result.error == ControlCodecError::InvalidMessageDirection);
    CHECK(std::string(control_codec_error_code(result.error)) == "invalid_message_direction");
}

void test_reject_header_fields_in_json() {
    const auto result = decode_client(
        "{\"message_type\":\"ping\","
        "\"request_id\":7}");
    CHECK(!result);
    CHECK(result.error == ControlCodecError::ForbiddenField);
}

void test_reject_missing_required_field() {
    const auto result = decode_client(
        "{\"message_type\":\"hello\","
        "\"client_name\":\"client\"}");
    CHECK(!result);
    CHECK(result.error == ControlCodecError::MissingRequiredField);
}

void test_reject_invalid_field_type() {
    const auto result = decode_client(
        "{\"message_type\":\"synthesize\","
        "\"text\":\"hello\","
        "\"language\":7}");
    CHECK(!result);
    CHECK(result.error == ControlCodecError::InvalidFieldType);
}

void test_reject_invalid_shutdown_mode() {
    const auto result = decode_client(
        "{\"message_type\":\"shutdown\","
        "\"mode\":\"wait\"}");
    CHECK(!result);
    CHECK(result.error == ControlCodecError::InvalidFieldType);
}

void test_reject_error_json_header_fields() {
    const auto result = decode_error_message(bytes_from_string(
        "{\"message_type\":\"error\","
        "\"request_id\":1,"
        "\"category\":\"request_error\","
        "\"code\":\"unknown_request_id\","
        "\"message\":\"bad id\"}"));
    CHECK(!result);
    CHECK(result.error == ControlCodecError::ForbiddenField);
}

} // namespace

int main() {
    test_decode_hello();
    test_decode_synthesize_with_instruction_and_output();
    test_decode_ready();
    test_encode_ping_round_trip();
    test_decode_started_audio_format();
    test_decode_error_json();
    test_encode_error_json();
    test_reject_invalid_json();
    test_reject_non_object_payload();
    test_reject_missing_message_type();
    test_reject_invalid_message_type();
    test_reject_unknown_message_type();
    test_reject_invalid_direction();
    test_reject_header_fields_in_json();
    test_reject_missing_required_field();
    test_reject_invalid_field_type();
    test_reject_invalid_shutdown_mode();
    test_reject_error_json_header_fields();
    return 0;
}
