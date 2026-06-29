#include <qwen_tts_bridge/session.hpp>
#include <qwen_tts_bridge/protocol/framing.hpp>
#include <qwen_tts_bridge/transport/stdio/StdIoTransport.hpp>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
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

std::vector<std::byte> frame_bytes(
    FrameType frame_type,
    RequestId request_id,
    const std::vector<std::byte>& payload) {
    const EncodeResult encoded = encode_frame(frame_type, request_id, payload);
    CHECK(encoded);
    return encoded.bytes;
}

std::vector<std::byte> control_frame_bytes(
    RequestId request_id,
    const std::string& json) {
    return frame_bytes(FrameType::ControlJson, request_id, bytes_from_string(json));
}

std::vector<std::byte> ready_frame_bytes(RequestId request_id = 0) {
    return control_frame_bytes(
        request_id,
        "{\"message_type\":\"ready\","
        "\"worker_version\":\"0.2.0\","
        "\"session_id\":\"scripted\","
        "\"warmed_up\":true,"
        "\"capabilities\":{"
        "\"streaming\":true,"
        "\"cancellation\":true,"
        "\"instructions\":true,"
        "\"voice_clone\":false"
        "}}");
}

std::vector<std::byte> audio_frame_bytes(RequestId request_id) {
    return frame_bytes(
        FrameType::AudioPcm,
        request_id,
        std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}});
}

class ScriptedTransport final : public ITransport {
public:
    bool start_return = true;
    std::vector<SendResult> send_results;
    std::vector<Bytes> chunks_on_send;
    std::vector<std::string> errors_on_send;
    bool exit_on_send = false;
    int exit_status = 0;
    int send_count = 0;

    bool start(
        ReceiveHandler receive_handler,
        ErrorHandler error_handler,
        ExitHandler exit_handler) override {
        receive_handler_ = std::move(receive_handler);
        error_handler_ = std::move(error_handler);
        exit_handler_ = std::move(exit_handler);
        running_ = start_return;
        return start_return;
    }

    SendResult send(const std::byte* data, std::size_t size) override {
        if (!running_ || (data == nullptr && size != 0)) {
            return SendResult::Closed;
        }

        ++send_count;
        SendResult send_result = SendResult::Accepted;
        if (!send_results.empty()) {
            send_result = send_results.front();
            send_results.erase(send_results.begin());
        }
        if (send_result != SendResult::Accepted) {
            return send_result;
        }

        for (const auto& error : errors_on_send) {
            error_handler_(error);
        }
        for (auto chunk : chunks_on_send) {
            receive_handler_(std::move(chunk));
        }
        if (exit_on_send) {
            running_ = false;
            exit_handler_(exit_status);
        }
        return SendResult::Accepted;
    }

    bool is_running() const override {
        return running_;
    }

    void stop() override {
        running_ = false;
    }

    void emit(Bytes bytes) {
        receive_handler_(std::move(bytes));
    }

private:
    ReceiveHandler receive_handler_;
    ErrorHandler error_handler_;
    ExitHandler exit_handler_;
    bool running_ = false;
};

StdIoTransportOptions make_worker_options(int mock_chunks) {
    StdIoTransportOptions options;
    options.arguments = {
        QWEN_TTS_BRIDGE_TEST_PYTHON_EXECUTABLE,
        "-m",
        "qwen_tts_bridge_worker.main",
        "--mock",
        "--mock-chunks",
        std::to_string(mock_chunks)
    };
    options.working_directory = QWEN_TTS_BRIDGE_TEST_WORKER_DIR;
    options.shutdown_timeout = std::chrono::seconds(5);
    return options;
}

std::unique_ptr<ITransport> make_transport(int mock_chunks) {
    return std::make_unique<StdIoTransport>(make_worker_options(mock_chunks));
}

WorkerSession make_session(int mock_chunks) {
    WorkerSessionOptions options;
    options.client_name = "worker-session-test";
    options.client_version = "0.2.0";
    options.startup_timeout = std::chrono::seconds(5);
    return WorkerSession(make_transport(mock_chunks), options);
}

WorkerSession make_scripted_session(
    std::unique_ptr<ScriptedTransport> transport,
    WorkerSessionOptions options = {}) {
    options.client_name = "worker-session-test";
    options.client_version = "0.2.0";
    if (options.startup_timeout == std::chrono::milliseconds{0}) {
        options.startup_timeout = std::chrono::seconds(5);
    }
    return WorkerSession(std::move(transport), options);
}

WorkerSessionEvent wait_for_event(
    WorkerSession& session,
    WorkerSessionEventType type) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        WorkerSessionEvent event;
        const bool got_event = session.wait_for_event(event, std::chrono::milliseconds(100));
        if (!got_event) {
            continue;
        }
        if (event.type == type) {
            return event;
        }
        CHECK(event.type == WorkerSessionEventType::Control ||
              event.type == WorkerSessionEventType::Audio);
    }

    CHECK(false);
    return WorkerSessionEvent();
}

WorkerSessionEvent wait_for_control(
    WorkerSession& session,
    ControlMessageType message_type,
    RequestId request_id) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        WorkerSessionEvent event;
        const bool got_event = session.wait_for_event(event, std::chrono::milliseconds(100));
        if (!got_event) {
            continue;
        }

        CHECK(event.type == WorkerSessionEventType::Control ||
              event.type == WorkerSessionEventType::Audio ||
              event.type == WorkerSessionEventType::Exited);

        if (event.type != WorkerSessionEventType::Control) {
            continue;
        }
        if (event.request_id == request_id &&
            control_message_type(event.control) == message_type) {
            return event;
        }
    }

    CHECK(false);
    return WorkerSessionEvent();
}

void test_handshake_ping_shutdown() {
    WorkerSession session = make_session(1);

    CHECK(session.start());
    CHECK(session.is_ready());

    ReadyMessage ready;
    CHECK(session.ready_message(ready));
    CHECK(ready.worker_version == "0.2.0");
    CHECK(ready.capabilities.streaming);
    CHECK(ready.capabilities.cancellation);

    const auto ready_event = wait_for_control(session, ControlMessageType::Ready, 0);
    CHECK(control_message_type(ready_event.control) == ControlMessageType::Ready);

    ReadyMessage invalid_outbound;
    CHECK(!session.send_control(0, ControlMessage{invalid_outbound}));
    const auto session_error = wait_for_event(session, WorkerSessionEventType::SessionError);
    CHECK(!session_error.message.empty());

    PingMessage ping;
    ping.has_sequence = true;
    ping.sequence = 17;
    CHECK(session.send_control(0, ControlMessage{ping}));

    const auto pong_event = wait_for_control(session, ControlMessageType::Pong, 0);
    const auto& pong = std::get<PongMessage>(pong_event.control);
    CHECK(pong.has_sequence);
    CHECK(pong.sequence == 17);

    ShutdownMessage shutdown;
    CHECK(session.send_control(0, ControlMessage{shutdown}));
    wait_for_control(session, ControlMessageType::ShutdownAck, 0);
    wait_for_event(session, WorkerSessionEventType::Exited);

    session.stop();
    CHECK(!session.is_running());
}

void test_synthesize_routes_control_audio_and_completed() {
    WorkerSession session = make_session(2);

    CHECK(session.start());
    wait_for_control(session, ControlMessageType::Ready, 0);

    SynthesizeMessage request;
    request.text = "Hello from WorkerSession.";
    request.language = "English";
    request.speaker = "default";
    request.instruction = "Speak calmly.";

    CHECK(session.send_control(1, ControlMessage{request}));

    const auto queued = wait_for_control(session, ControlMessageType::Queued, 1);
    const auto& queued_payload = std::get<QueuedMessage>(queued.control);
    CHECK(queued_payload.has_position);
    CHECK(queued_payload.position == 1);

    const auto started = wait_for_control(session, ControlMessageType::Started, 1);
    const auto& started_payload = std::get<StartedMessage>(started.control);
    CHECK(started_payload.audio_format.sample_format == "s16le");
    CHECK(started_payload.audio_format.sample_rate == 24000);
    CHECK(started_payload.audio_format.channels == 1);

    const auto audio = wait_for_event(session, WorkerSessionEventType::Audio);
    CHECK(audio.request_id == 1);
    CHECK(!audio.audio.empty());

    wait_for_control(session, ControlMessageType::Completed, 1);

    ShutdownMessage shutdown;
    CHECK(session.send_control(0, ControlMessage{shutdown}));
    wait_for_control(session, ControlMessageType::ShutdownAck, 0);
    wait_for_event(session, WorkerSessionEventType::Exited);
    session.stop();
}

void test_start_fails_fast_on_malformed_ready() {
    auto transport = std::make_unique<ScriptedTransport>();
    transport->chunks_on_send.push_back(control_frame_bytes(
        0,
        "{\"message_type\":\"ready\"}"));

    WorkerSessionOptions options;
    options.startup_timeout = std::chrono::seconds(5);
    WorkerSession session = make_scripted_session(std::move(transport), options);

    const auto started = std::chrono::steady_clock::now();
    CHECK(!session.start());
    const auto elapsed = std::chrono::steady_clock::now() - started;
    CHECK(elapsed < std::chrono::seconds(2));

    const auto event = wait_for_event(session, WorkerSessionEventType::ProtocolError);
    CHECK(!event.message.empty());
}

void test_start_rejects_ready_with_request_id() {
    auto transport = std::make_unique<ScriptedTransport>();
    transport->chunks_on_send.push_back(ready_frame_bytes(123));
    WorkerSession session = make_scripted_session(std::move(transport));

    CHECK(!session.start());
    wait_for_event(session, WorkerSessionEventType::ProtocolError);
}

void test_duplicate_ready_fails_session() {
    auto transport = std::make_unique<ScriptedTransport>();
    auto* raw_transport = transport.get();
    transport->chunks_on_send.push_back(ready_frame_bytes());
    WorkerSession session = make_scripted_session(std::move(transport));

    CHECK(session.start());
    wait_for_control(session, ControlMessageType::Ready, 0);

    raw_transport->emit(ready_frame_bytes());
    wait_for_event(session, WorkerSessionEventType::ProtocolError);
    CHECK(session.state() == WorkerSessionState::Failed);
}

void test_unexpected_shutdown_ack_fails_session() {
    auto transport = std::make_unique<ScriptedTransport>();
    auto* raw_transport = transport.get();
    transport->chunks_on_send.push_back(ready_frame_bytes());
    WorkerSession session = make_scripted_session(std::move(transport));

    CHECK(session.start());
    wait_for_control(session, ControlMessageType::Ready, 0);

    raw_transport->emit(control_frame_bytes(0, "{\"message_type\":\"shutdown_ack\"}"));
    wait_for_event(session, WorkerSessionEventType::ProtocolError);
    CHECK(session.state() == WorkerSessionState::Failed);
}

void test_start_rejects_audio_before_ready() {
    auto transport = std::make_unique<ScriptedTransport>();
    transport->chunks_on_send.push_back(audio_frame_bytes(1));
    WorkerSession session = make_scripted_session(std::move(transport));

    CHECK(!session.start());
    wait_for_event(session, WorkerSessionEventType::ProtocolError);
}

void test_start_fails_on_exit_before_ready() {
    auto transport = std::make_unique<ScriptedTransport>();
    transport->exit_on_send = true;
    transport->exit_status = 7;
    WorkerSession session = make_scripted_session(std::move(transport));

    CHECK(!session.start());
    const auto event = wait_for_event(session, WorkerSessionEventType::Exited);
    CHECK(event.exit_status == 7);
}

void test_start_fails_on_transport_error() {
    auto transport = std::make_unique<ScriptedTransport>();
    transport->errors_on_send.push_back("scripted transport error");
    WorkerSession session = make_scripted_session(std::move(transport));

    CHECK(!session.start());
    const auto event = wait_for_event(session, WorkerSessionEventType::TransportError);
    CHECK(event.message.find("scripted") != std::string::npos);
}

void test_start_fails_on_hello_send_failure() {
    auto transport = std::make_unique<ScriptedTransport>();
    transport->send_results.push_back(SendResult::Failed);
    WorkerSession session = make_scripted_session(std::move(transport));

    const auto started = std::chrono::steady_clock::now();
    CHECK(!session.start());
    const auto elapsed = std::chrono::steady_clock::now() - started;
    CHECK(elapsed < std::chrono::seconds(2));

    const auto event = wait_for_event(session, WorkerSessionEventType::TransportError);
    CHECK(event.message.find("failed to send control") != std::string::npos);
    CHECK(!session.is_ready());
    CHECK(!session.is_running());
}

void test_send_control_failure_reports_transport_error() {
    auto transport = std::make_unique<ScriptedTransport>();
    transport->send_results.push_back(SendResult::Accepted);
    transport->send_results.push_back(SendResult::Failed);
    transport->chunks_on_send.push_back(ready_frame_bytes());
    WorkerSession session = make_scripted_session(std::move(transport));

    CHECK(session.start());
    wait_for_control(session, ControlMessageType::Ready, 0);

    PingMessage ping;
    CHECK(!session.send_control(0, ControlMessage{ping}));

    const auto event = wait_for_event(session, WorkerSessionEventType::TransportError);
    CHECK(event.message.find("failed to send control") != std::string::npos);
    CHECK(session.state() == WorkerSessionState::Failed);
}

void test_queue_overflow_prevents_startup_success() {
    auto transport = std::make_unique<ScriptedTransport>();
    transport->chunks_on_send.push_back(ready_frame_bytes());

    WorkerSessionOptions options;
    options.startup_timeout = std::chrono::seconds(5);
    options.max_event_queue_bytes = 1;
    WorkerSession session = make_scripted_session(std::move(transport), options);

    CHECK(!session.start());
    const auto event = wait_for_event(session, WorkerSessionEventType::SessionError);
    CHECK(event.message.find("overflow") != std::string::npos);
}

void test_invalid_options_and_repeated_start_are_rejected() {
    {
        auto transport = std::make_unique<ScriptedTransport>();
        WorkerSessionOptions options;
        options.max_event_queue_bytes = 0;
        WorkerSession session = make_scripted_session(std::move(transport), options);
        CHECK(!session.start());
    }

    auto transport = std::make_unique<ScriptedTransport>();
    transport->chunks_on_send.push_back(ready_frame_bytes());
    WorkerSession session = make_scripted_session(std::move(transport));

    CHECK(session.start());
    wait_for_control(session, ControlMessageType::Ready, 0);
    session.stop();
    CHECK(!session.start());
}

} // namespace

int main() {
    test_handshake_ping_shutdown();
    test_synthesize_routes_control_audio_and_completed();
    test_start_fails_fast_on_malformed_ready();
    test_start_rejects_ready_with_request_id();
    test_duplicate_ready_fails_session();
    test_unexpected_shutdown_ack_fails_session();
    test_start_rejects_audio_before_ready();
    test_start_fails_on_exit_before_ready();
    test_start_fails_on_transport_error();
    test_start_fails_on_hello_send_failure();
    test_send_control_failure_reports_transport_error();
    test_queue_overflow_prevents_startup_success();
    test_invalid_options_and_repeated_start_are_rejected();
    return 0;
}
