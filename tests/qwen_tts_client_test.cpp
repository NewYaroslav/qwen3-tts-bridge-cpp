#include <qwen_tts_bridge/client.hpp>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iostream>
#include <mutex>
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

struct RequestProbe {
    std::mutex mutex;
    std::condition_variable condition;
    std::size_t audio_chunks = 0;
    std::size_t audio_bytes = 0;
    bool completed = false;
    bool cancelled = false;
    std::vector<TtsError> errors;
    std::vector<RequestId> audio_request_ids;
};

StdIoTransportOptions make_worker_options(
    int mock_chunks,
    double chunk_delay_seconds = 0.0) {
    StdIoTransportOptions options;
    options.arguments = {
        QWEN_TTS_BRIDGE_TEST_PYTHON_EXECUTABLE,
        "-m",
        "qwen_tts_bridge_worker.main",
        "--mock",
        "--mock-chunks",
        std::to_string(mock_chunks),
        "--mock-chunk-delay",
        std::to_string(chunk_delay_seconds)
    };
    options.working_directory = QWEN_TTS_BRIDGE_TEST_WORKER_DIR;
    options.shutdown_timeout = std::chrono::seconds(5);
    return options;
}

QwenTtsClientOptions make_client_options() {
    QwenTtsClientOptions options;
    options.session.client_name = "qwen-tts-client-test";
    options.session.client_version = "0.2.0";
    options.session.startup_timeout = std::chrono::seconds(5);
    return options;
}

TtsCallbacks make_callbacks(RequestProbe& probe) {
    TtsCallbacks callbacks;
    callbacks.on_audio = [&probe](const PcmChunk& chunk) {
        std::lock_guard<std::mutex> lock(probe.mutex);
        ++probe.audio_chunks;
        probe.audio_bytes += chunk.bytes.size();
        probe.audio_request_ids.push_back(chunk.request_id);
        probe.condition.notify_all();
    };
    callbacks.on_completed = [&probe]() {
        std::lock_guard<std::mutex> lock(probe.mutex);
        probe.completed = true;
        probe.condition.notify_all();
    };
    callbacks.on_cancelled = [&probe]() {
        std::lock_guard<std::mutex> lock(probe.mutex);
        probe.cancelled = true;
        probe.condition.notify_all();
    };
    callbacks.on_error = [&probe](const TtsError& error) {
        std::lock_guard<std::mutex> lock(probe.mutex);
        probe.errors.push_back(error);
        probe.condition.notify_all();
    };
    return callbacks;
}

template <class Predicate>
bool wait_for_probe(RequestProbe& probe, Predicate predicate) {
    std::unique_lock<std::mutex> lock(probe.mutex);
    return probe.condition.wait_for(
        lock,
        std::chrono::seconds(5),
        [&probe, &predicate]() {
            return predicate(probe);
        });
}

TtsRequest make_request(const std::string& text) {
    TtsRequest request;
    request.text = text;
    request.language = "English";
    request.speaker = "default";
    request.instruction = "Speak calmly.";
    return request;
}

void test_synthesize_async_delivers_audio_and_completed() {
    QwenTtsClient client;
    CHECK(client.start(make_worker_options(2), make_client_options()));
    CHECK(client.is_running());

    RequestProbe probe;
    const RequestId request_id = client.synthesize_async(
        "Hello from QwenTtsClient.",
        make_callbacks(probe));

    CHECK(request_id != 0);
    CHECK(wait_for_probe(probe, [](const RequestProbe& state) {
        return state.completed || !state.errors.empty();
    }));

    {
        std::lock_guard<std::mutex> lock(probe.mutex);
        CHECK(probe.errors.empty());
        CHECK(probe.completed);
        CHECK(!probe.cancelled);
        CHECK(probe.audio_chunks > 0);
        CHECK(probe.audio_bytes > 0);
        for (const RequestId audio_request_id : probe.audio_request_ids) {
            CHECK(audio_request_id == request_id);
        }
    }

    client.stop();
    CHECK(!client.is_running());
}

void test_multiple_async_requests_complete() {
    QwenTtsClient client;
    CHECK(client.start(make_worker_options(1), make_client_options()));

    RequestProbe first;
    RequestProbe second;

    const RequestId first_id = client.synthesize_async(
        make_request("First queued request."),
        make_callbacks(first));
    const RequestId second_id = client.synthesize_async(
        make_request("Second queued request."),
        make_callbacks(second));

    CHECK(first_id != 0);
    CHECK(second_id != 0);
    CHECK(first_id != second_id);

    CHECK(wait_for_probe(first, [](const RequestProbe& state) {
        return state.completed || !state.errors.empty();
    }));
    CHECK(wait_for_probe(second, [](const RequestProbe& state) {
        return state.completed || !state.errors.empty();
    }));

    {
        std::lock_guard<std::mutex> lock(first.mutex);
        CHECK(first.errors.empty());
        CHECK(first.completed);
        CHECK(first.audio_chunks > 0);
    }
    {
        std::lock_guard<std::mutex> lock(second.mutex);
        CHECK(second.errors.empty());
        CHECK(second.completed);
        CHECK(second.audio_chunks > 0);
    }

    client.stop();
}

void test_cancel_queued_request() {
    QwenTtsClient client;
    CHECK(client.start(make_worker_options(5, 0.05), make_client_options()));

    RequestProbe first;
    RequestProbe second;

    const RequestId first_id = client.synthesize_async(
        make_request("Long first request."),
        make_callbacks(first));
    const RequestId second_id = client.synthesize_async(
        make_request("Queued request to cancel."),
        make_callbacks(second));

    CHECK(first_id != 0);
    CHECK(second_id != 0);
    CHECK(client.cancel(second_id));

    CHECK(wait_for_probe(second, [](const RequestProbe& state) {
        return state.cancelled || !state.errors.empty();
    }));
    CHECK(wait_for_probe(first, [](const RequestProbe& state) {
        return state.completed || !state.errors.empty();
    }));

    {
        std::lock_guard<std::mutex> lock(second.mutex);
        CHECK(second.errors.empty());
        CHECK(second.cancelled);
        CHECK(!second.completed);
    }
    {
        std::lock_guard<std::mutex> lock(first.mutex);
        CHECK(first.errors.empty());
        CHECK(first.completed);
    }

    client.stop();
}

void test_request_error_routes_to_callback() {
    QwenTtsClient client;
    CHECK(client.start(make_worker_options(1), make_client_options()));

    RequestProbe probe;
    TtsRequest request = make_request("Unsupported format request.");
    request.output.sample_rate = 48000;

    const RequestId request_id = client.synthesize_async(
        std::move(request),
        make_callbacks(probe));

    CHECK(request_id != 0);
    CHECK(wait_for_probe(probe, [](const RequestProbe& state) {
        return !state.errors.empty();
    }));

    {
        std::lock_guard<std::mutex> lock(probe.mutex);
        CHECK(!probe.completed);
        CHECK(!probe.cancelled);
        CHECK(probe.errors.size() == 1);
        CHECK(probe.errors.front().request_id == request_id);
        CHECK(probe.errors.front().category == "request_error");
        CHECK(probe.errors.front().code == "unsupported_audio_format");
    }

    client.stop();
}

} // namespace

int main() {
    test_synthesize_async_delivers_audio_and_completed();
    test_multiple_async_requests_complete();
    test_cancel_queued_request();
    test_request_error_routes_to_callback();
    return 0;
}
