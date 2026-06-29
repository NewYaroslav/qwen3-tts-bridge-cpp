#include <qwen_tts_bridge/protocol/framing.hpp>
#include <qwen_tts_bridge/transport/stdio/StdIoTransport.hpp>

#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <utility>
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

bool contains(const Frame& frame, const std::string& text) {
    return string_from_bytes(frame.payload).find(text) != std::string::npos;
}

class FrameCollector {
public:
    void on_bytes(ITransport::Bytes bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        parser_.append(bytes);

        while (true) {
            ParseResult result = parser_.parse_next();
            if (result.status == ParseStatus::NeedMoreData) {
                break;
            }
            if (result.status == ParseStatus::FatalError) {
                errors_.push_back(result.message);
                break;
            }
            frames_.push_back(std::move(result.frame));
        }

        condition_.notify_all();
    }

    void on_error(std::string message) {
        std::lock_guard<std::mutex> lock(mutex_);
        errors_.push_back(std::move(message));
        condition_.notify_all();
    }

    void on_exit(int status) {
        std::lock_guard<std::mutex> lock(mutex_);
        exited_ = true;
        exit_status_ = status;
        condition_.notify_all();
    }

    Frame wait_for_frame(const std::string& expected_text) {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool ready = condition_.wait_for(lock, std::chrono::seconds(5), [&]() {
            for (const auto& frame : frames_) {
                if (contains(frame, expected_text)) {
                    return true;
                }
            }
            return false;
        });
        CHECK(ready);

        for (auto it = frames_.begin(); it != frames_.end(); ++it) {
            if (contains(*it, expected_text)) {
                Frame frame = std::move(*it);
                frames_.erase(it);
                return frame;
            }
        }

        CHECK(false);
        return Frame();
    }

    int wait_for_exit() {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool ready = condition_.wait_for(lock, std::chrono::seconds(5), [&]() {
            return exited_;
        });
        CHECK(ready);
        return exit_status_;
    }

    bool has_errors() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !errors_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    FrameParser parser_;
    std::deque<Frame> frames_;
    std::vector<std::string> errors_;
    bool exited_ = false;
    int exit_status_ = -1;
};

void send_control(
    StdIoTransport& transport,
    RequestId request_id,
    const std::string& payload) {
    const EncodeResult encoded = encode_frame(
        FrameType::ControlJson,
        request_id,
        bytes_from_string(payload));
    CHECK(encoded);
    CHECK(transport.send(encoded.bytes.data(), encoded.bytes.size()));
}

void test_mock_worker_handshake_over_stdio_transport() {
    StdIoTransportOptions options;
    options.arguments = {
        QWEN_TTS_BRIDGE_TEST_PYTHON_EXECUTABLE,
        "-m",
        "qwen_tts_bridge_worker.main",
        "--mock",
        "--mock-chunks",
        "1"
    };
    options.working_directory = QWEN_TTS_BRIDGE_TEST_WORKER_DIR;
    options.shutdown_timeout = std::chrono::seconds(5);

    FrameCollector collector;
    StdIoTransport transport(options);

    const bool started = transport.start(
        [&](ITransport::Bytes bytes) {
            collector.on_bytes(std::move(bytes));
        },
        [&](std::string message) {
            collector.on_error(std::move(message));
        },
        [&](int status) {
            collector.on_exit(status);
        });

    CHECK(started);
    CHECK(transport.is_running());

    send_control(
        transport,
        0,
        "{\"message_type\":\"hello\",\"client_name\":\"stdio-transport-test\",\"client_version\":\"0.2.0\"}");
    const Frame ready = collector.wait_for_frame("\"message_type\":\"ready\"");
    CHECK(ready.header.frame_type == FrameType::ControlJson);
    CHECK(ready.header.request_id == 0);

    send_control(transport, 0, "{\"message_type\":\"ping\",\"sequence\":17}");
    const Frame pong = collector.wait_for_frame("\"message_type\":\"pong\"");
    CHECK(contains(pong, "\"sequence\":17"));

    send_control(transport, 0, "{\"message_type\":\"shutdown\",\"mode\":\"cancel\"}");
    const Frame shutdown_ack = collector.wait_for_frame("\"message_type\":\"shutdown_ack\"");
    CHECK(shutdown_ack.header.request_id == 0);

    CHECK(collector.wait_for_exit() == 0);
    transport.stop();
    CHECK(!transport.is_running());
    CHECK(!collector.has_errors());
}

} // namespace

int main() {
    test_mock_worker_handshake_over_stdio_transport();
    return 0;
}
