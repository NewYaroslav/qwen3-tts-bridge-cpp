#include <qwen_tts_bridge/session.hpp>
#include <qwen_tts_bridge/transport/stdio/StdIoTransport.hpp>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <variant>

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

} // namespace

int main() {
    test_handshake_ping_shutdown();
    test_synthesize_routes_control_audio_and_completed();
    return 0;
}
