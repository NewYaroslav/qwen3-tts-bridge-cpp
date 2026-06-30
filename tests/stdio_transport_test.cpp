#include <qwen_tts_bridge/protocol/framing.hpp>
#include <qwen_tts_bridge/transport.hpp>

#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
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

StdIoTransportOptions make_python_options(
    std::string script,
    std::chrono::milliseconds shutdown_timeout = std::chrono::seconds(5)) {
    StdIoTransportOptions options;
    options.arguments = {
        QWEN_TTS_BRIDGE_TEST_PYTHON_EXECUTABLE,
        "-c",
        std::move(script)
    };
    options.shutdown_timeout = shutdown_timeout;
    return options;
}

class RawCollector {
public:
    void on_bytes(ITransport::Bytes bytes) {
        std::lock_guard<std::mutex> lock(mutex_);
        bytes_.append(string_from_bytes(bytes));
        event_order_.push_back('R');
        condition_.notify_all();
    }

    void on_error(std::string message) {
        std::lock_guard<std::mutex> lock(mutex_);
        event_order_.push_back('!');
        errors_.push_back(std::move(message));
        condition_.notify_all();
    }

    void on_exit(int status) {
        std::lock_guard<std::mutex> lock(mutex_);
        exited_ = true;
        exit_status_ = status;
        event_order_.push_back('E');
        condition_.notify_all();
    }

    void on_stderr(std::string message) {
        std::lock_guard<std::mutex> lock(mutex_);
        stderr_.append(std::move(message));
        condition_.notify_all();
    }

    void wait_for_text(const std::string& expected_text) {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool ready = condition_.wait_for(lock, std::chrono::seconds(5), [&]() {
            return bytes_.find(expected_text) != std::string::npos;
        });
        CHECK(ready);
    }

    void wait_for_stderr(const std::string& expected_text) {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool ready = condition_.wait_for(lock, std::chrono::seconds(5), [&]() {
            return stderr_.find(expected_text) != std::string::npos;
        });
        CHECK(ready);
    }

    void wait_for_error(const std::string& expected_text) {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool ready = condition_.wait_for(lock, std::chrono::seconds(5), [&]() {
            for (const auto& error : errors_) {
                if (error.find(expected_text) != std::string::npos) {
                    return true;
                }
            }
            return false;
        });
        CHECK(ready);
    }

    int wait_for_exit() {
        std::unique_lock<std::mutex> lock(mutex_);
        const bool ready = condition_.wait_for(lock, std::chrono::seconds(5), [&]() {
            return exited_;
        });
        CHECK(ready);
        return exit_status_;
    }

    bool has_exit() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return exited_;
    }

    bool has_errors() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !errors_.empty();
    }

    bool contains_text(const std::string& expected_text) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return bytes_.find(expected_text) != std::string::npos;
    }

    bool has_receive_after_exit() const {
        std::lock_guard<std::mutex> lock(mutex_);
        bool saw_exit = false;
        for (const char event : event_order_) {
            if (event == 'E') {
                saw_exit = true;
            }
            else if (event == 'R' && saw_exit) {
                return true;
            }
        }
        return false;
    }

    std::size_t error_count(const std::string& expected_text) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::size_t count = 0;
        for (const auto& error : errors_) {
            if (error.find(expected_text) != std::string::npos) {
                ++count;
            }
        }
        return count;
    }

    bool has_error_before_exit(const std::string& expected_text) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::size_t error_index = event_order_.size();
        std::size_t exit_index = event_order_.size();

        std::size_t seen_errors = 0;
        for (std::size_t index = 0; index < event_order_.size(); ++index) {
            if (event_order_[index] == '!') {
                const auto& error = errors_.at(seen_errors);
                if (error.find(expected_text) != std::string::npos) {
                    error_index = index;
                }
                ++seen_errors;
            }
            else if (event_order_[index] == 'E') {
                exit_index = index;
                break;
            }
        }

        return error_index < exit_index;
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::string bytes_;
    std::string stderr_;
    std::vector<std::string> errors_;
    std::vector<char> event_order_;
    bool exited_ = false;
    int exit_status_ = -1;
};

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
    CHECK(transport.send(encoded.bytes.data(), encoded.bytes.size()) ==
          SendResult::Accepted);
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

void test_send_before_start_and_empty_send() {
    StdIoTransport transport(make_python_options("import time; time.sleep(60)"));

    std::byte byte{0x42};
    CHECK(transport.send(nullptr, 0) == SendResult::Accepted);
    CHECK(transport.send(&byte, 1) == SendResult::Closed);
    transport.stop();
}

void test_missing_executable_start_failure() {
#if defined(_WIN32)
    StdIoTransportOptions options;
    options.arguments = {"definitely_missing_qwen_tts_worker_for_transport_test.exe"};

    RawCollector collector;
    StdIoTransport transport(options);

    CHECK(!transport.start(
        [&](ITransport::Bytes bytes) {
            collector.on_bytes(std::move(bytes));
        },
        [&](std::string message) {
            collector.on_error(std::move(message));
        },
        [&](int status) {
            collector.on_exit(status);
        }));
    CHECK(!transport.is_running());

    CHECK(!transport.start(
        [&](ITransport::Bytes bytes) {
            collector.on_bytes(std::move(bytes));
        },
        [&](std::string message) {
            collector.on_error(std::move(message));
        },
        [&](int status) {
            collector.on_exit(status);
        }));
    transport.stop();
#endif
}

void test_second_start_is_rejected() {
    RawCollector collector;
    StdIoTransport transport(make_python_options(
        "import time; time.sleep(60)",
        std::chrono::milliseconds(100)));

    CHECK(transport.start(
        [&](ITransport::Bytes bytes) {
            collector.on_bytes(std::move(bytes));
        },
        [&](std::string message) {
            collector.on_error(std::move(message));
        },
        [&](int status) {
            collector.on_exit(status);
        }));

    CHECK(!transport.start(
        [](ITransport::Bytes) {},
        [](std::string) {},
        [](int) {}));

    transport.stop();
    CHECK(!transport.is_running());
}

void test_repeated_stop_and_destructor_shutdown() {
    {
        RawCollector collector;
        StdIoTransport transport(make_python_options(
            "import time; time.sleep(60)",
            std::chrono::milliseconds(100)));

        CHECK(transport.start(
            [&](ITransport::Bytes bytes) {
                collector.on_bytes(std::move(bytes));
            },
            [&](std::string message) {
                collector.on_error(std::move(message));
            },
            [&](int status) {
                collector.on_exit(status);
            }));
    }

    StdIoTransport transport(make_python_options(
        "import time; time.sleep(60)",
        std::chrono::milliseconds(100)));
    transport.stop();
    transport.stop();
}

void test_stderr_is_delivered_separately() {
    RawCollector collector;
    StdIoTransportOptions options = make_python_options(
        "import sys; sys.stderr.write('stderr-ok'); sys.stderr.flush()");
    options.stderr_handler = [&](std::string message) {
        collector.on_stderr(std::move(message));
    };

    StdIoTransport transport(options);
    CHECK(transport.start(
        [&](ITransport::Bytes bytes) {
            collector.on_bytes(std::move(bytes));
        },
        [&](std::string message) {
            collector.on_error(std::move(message));
        },
        [&](int status) {
            collector.on_exit(status);
        }));

    collector.wait_for_stderr("stderr-ok");
    collector.wait_for_exit();
    transport.stop();
    CHECK(!collector.has_errors());
}

void test_stop_forces_kill_after_timeout() {
    RawCollector collector;
    StdIoTransport transport(make_python_options(
        "import time; time.sleep(60)",
        std::chrono::milliseconds(100)));

    CHECK(transport.start(
        [&](ITransport::Bytes bytes) {
            collector.on_bytes(std::move(bytes));
        },
        [&](std::string message) {
            collector.on_error(std::move(message));
        },
        [&](int status) {
            collector.on_exit(status);
        }));

    const auto started = std::chrono::steady_clock::now();
    transport.stop();
    const auto elapsed = std::chrono::steady_clock::now() - started;

    CHECK(elapsed < std::chrono::seconds(5));
    CHECK(!transport.is_running());
    CHECK(collector.has_exit());
}

void test_stop_from_receive_callback() {
    RawCollector collector;
    StdIoTransport transport(make_python_options(
        "import sys, time; sys.stdout.buffer.write(b'stop-from-callback'); "
        "sys.stdout.flush(); time.sleep(60)",
        std::chrono::milliseconds(100)));

    std::mutex mutex;
    std::condition_variable condition;
    bool callback_finished = false;

    CHECK(transport.start(
        [&](ITransport::Bytes bytes) {
            collector.on_bytes(std::move(bytes));
            transport.stop();

            {
                std::lock_guard<std::mutex> lock(mutex);
                callback_finished = true;
            }
            condition.notify_all();
        },
        [&](std::string message) {
            collector.on_error(std::move(message));
        },
        [&](int status) {
            collector.on_exit(status);
        }));

    {
        std::unique_lock<std::mutex> lock(mutex);
        const bool ready = condition.wait_for(lock, std::chrono::seconds(5), [&]() {
            return callback_finished;
        });
        CHECK(ready);
    }

    transport.stop();
    CHECK(!transport.is_running());
}

void test_stop_can_kill_worker_while_send_is_blocked() {
    RawCollector collector;
    StdIoTransport transport(make_python_options(
        "import time; time.sleep(60)",
        std::chrono::milliseconds(100)));

    CHECK(transport.start(
        [&](ITransport::Bytes bytes) {
            collector.on_bytes(std::move(bytes));
        },
        [&](std::string message) {
            collector.on_error(std::move(message));
        },
        [&](int status) {
            collector.on_exit(status);
        }));

    std::vector<std::byte> payload(16u * 1024u * 1024u, std::byte{0x31});
    std::atomic<bool> send_started{false};
    std::atomic<bool> send_finished{false};

    std::thread writer([&]() {
        send_started = true;
        transport.send(payload.data(), payload.size());
        send_finished = true;
    });

    for (int attempt = 0; attempt < 50 && !send_started; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(send_started);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const auto started = std::chrono::steady_clock::now();
    transport.stop();
    const auto elapsed = std::chrono::steady_clock::now() - started;

    CHECK(elapsed < std::chrono::seconds(5));
    writer.join();
    CHECK(send_finished);
    CHECK(!transport.is_running());
}

void test_stdout_is_delivered_before_exit_callback() {
    RawCollector collector;
    StdIoTransportOptions options = make_python_options(
        "import sys\n"
        "for index in range(1000):\n"
        "    sys.stdout.buffer.write((f'marker-{index:04d}\\n').encode('ascii'))\n"
        "sys.stdout.flush()\n");
    options.read_buffer_size = 64;

    StdIoTransport transport(options);
    CHECK(transport.start(
        [&](ITransport::Bytes bytes) {
            collector.on_bytes(std::move(bytes));
        },
        [&](std::string message) {
            collector.on_error(std::move(message));
        },
        [&](int status) {
            collector.on_exit(status);
        }));

    CHECK(collector.wait_for_exit() == 0);
    transport.stop();

    CHECK(collector.contains_text("marker-0000"));
    CHECK(collector.contains_text("marker-0999"));
    CHECK(!collector.has_receive_after_exit());
    CHECK(!collector.has_errors());
}

void test_invalid_callback_queue_limits_reject_start() {
    StdIoTransportOptions options = make_python_options("import time; time.sleep(60)");
    options.max_callback_queue_events = 0;

    RawCollector collector;
    StdIoTransport transport(options);

    CHECK(!transport.start(
        [&](ITransport::Bytes bytes) {
            collector.on_bytes(std::move(bytes));
        },
        [&](std::string message) {
            collector.on_error(std::move(message));
        },
        [&](int status) {
            collector.on_exit(status);
        }));
    CHECK(!transport.is_running());
}

void test_callback_queue_overflow_reports_error_and_stops_worker() {
    RawCollector collector;
    StdIoTransportOptions options = make_python_options(
        "import sys, time\n"
        "sys.stdout.buffer.write(b'x' * 262144)\n"
        "sys.stdout.flush()\n"
        "time.sleep(10)\n",
        std::chrono::milliseconds(500));
    options.read_buffer_size = 16;
    options.max_callback_queue_events = 4;
    options.max_callback_queue_bytes = 128;

    StdIoTransport transport(options);
    CHECK(transport.start(
        [&](ITransport::Bytes bytes) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            collector.on_bytes(std::move(bytes));
        },
        [&](std::string message) {
            collector.on_error(std::move(message));
        },
        [&](int status) {
            collector.on_exit(status);
        }));

    collector.wait_for_error("callback queue overflow");
    collector.wait_for_exit();
    transport.stop();
    CHECK(!transport.is_running());
    CHECK(collector.error_count("callback queue overflow") == 1);
    CHECK(collector.has_error_before_exit("callback queue overflow"));
}

void test_unicode_working_directory_and_argument() {
#if defined(_WIN32)
    namespace fs = std::filesystem;

    const std::string unicode_suffix = u8"qwen_tts_bridge_\u0442\u0435\u0441\u0442";
    const std::string unicode_argument = u8"\u041f\u0440\u0438\u0432\u0435\u0442";
    const fs::path directory = fs::temp_directory_path() / fs::u8path(unicode_suffix);
    fs::create_directories(directory);

    RawCollector collector;
    StdIoTransportOptions options = make_python_options(
        "import os, sys; "
        "sys.stdout.buffer.write((os.getcwd() + '|' + sys.argv[1]).encode('utf-8')); "
        "sys.stdout.flush()");
    options.arguments.push_back(unicode_argument);
    options.working_directory = directory.u8string();

    StdIoTransport transport(options);
    CHECK(transport.start(
        [&](ITransport::Bytes bytes) {
            collector.on_bytes(std::move(bytes));
        },
        [&](std::string message) {
            collector.on_error(std::move(message));
        },
        [&](int status) {
            collector.on_exit(status);
        }));

    collector.wait_for_text(unicode_suffix);
    collector.wait_for_text(unicode_argument);
    collector.wait_for_exit();
    transport.stop();

    fs::remove_all(directory);
#endif
}

} // namespace

int main() {
    test_mock_worker_handshake_over_stdio_transport();
    test_send_before_start_and_empty_send();
    test_missing_executable_start_failure();
    test_second_start_is_rejected();
    test_repeated_stop_and_destructor_shutdown();
    test_stderr_is_delivered_separately();
    test_stop_forces_kill_after_timeout();
    test_stop_from_receive_callback();
    test_stop_can_kill_worker_while_send_is_blocked();
    test_stdout_is_delivered_before_exit_callback();
    test_invalid_callback_queue_limits_reject_start();
    test_callback_queue_overflow_reports_error_and_stops_worker();
    test_unicode_working_directory_and_argument();
    return 0;
}
