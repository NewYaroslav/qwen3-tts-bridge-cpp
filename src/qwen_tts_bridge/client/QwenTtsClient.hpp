#pragma once

/// \file QwenTtsClient.hpp
/// \brief Async public facade for a persistent Qwen TTS worker.

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include <qwen_tts_bridge/client/ClientTypes.hpp>
#include <qwen_tts_bridge/session.hpp>
#include <qwen_tts_bridge/transport.hpp>

namespace qwen_tts_bridge {

/// \struct QwenTtsClientOptions
/// \brief Runtime options for the public async client facade.
struct QwenTtsClientOptions {
    /// \brief Options passed to the underlying worker session.
    WorkerSessionOptions session;

    /// \brief Maximum outbound control commands waiting for the writer thread.
    std::size_t max_outbound_commands = 4096u;
};

/// \class QwenTtsClient
/// \brief Starts a persistent worker and exposes async synthesis requests.
///
/// `start()` may block while the worker process starts, loads the model, warms
/// up, and sends `ready`. `synthesize_async()` does not wait for inference or
/// audio generation; it assigns a request ID, stores callbacks, and enqueues an
/// outbound command for the client's writer thread.
class QwenTtsClient final {
public:
    /// \brief Creates a stopped client.
    QwenTtsClient();

    /// \brief Stops the worker session if still running.
    ~QwenTtsClient();

    QwenTtsClient(const QwenTtsClient&) = delete;
    QwenTtsClient& operator=(const QwenTtsClient&) = delete;

    /// \brief Starts the client using an already configured transport.
    /// \param transport Transport implementation used by the worker session.
    /// \param options Client and session options.
    /// \return True when the worker reached ready and client threads started.
    bool start(
        std::unique_ptr<ITransport> transport,
        QwenTtsClientOptions options = {});

    /// \brief Starts the client using stdio transport options.
    /// \param transport_options Worker process launch options.
    /// \param options Client and session options.
    /// \return True when the worker reached ready and client threads started.
    bool start(
        const StdIoTransportOptions& transport_options,
        QwenTtsClientOptions options = {});

    /// \brief Starts the client with a worker executable path.
    /// \param worker_executable Worker executable path.
    /// \return True when the worker reached ready and client threads started.
    bool start(const std::string& worker_executable);

    /// \brief Enqueues an async synthesis request.
    /// \param request Request data. If `request.id` is zero, a new ID is assigned.
    /// \param callbacks Request callback set.
    /// \return Non-zero request ID on enqueue success, or zero on local failure.
    RequestId synthesize_async(TtsRequest request, TtsCallbacks callbacks);

    /// \brief Enqueues an async synthesis request with default options.
    /// \param text Spoken UTF-8 text.
    /// \param callbacks Request callback set.
    /// \return Non-zero request ID on enqueue success, or zero on local failure.
    RequestId synthesize_async(
        const std::string& text,
        TtsCallbacks callbacks);

    /// \brief Enqueues cancellation for a locally active request.
    /// \param request_id Request to cancel.
    /// \return True when the request was known and cancel was queued.
    bool cancel(RequestId request_id);

    /// \brief Returns whether the client is running and ready for requests.
    bool is_running() const;

    /// \brief Stops the client, worker session, and internal threads.
    ///
    /// This method is idempotent. It may block while the worker stops and
    /// internal threads join. It is safe to call from request callbacks.
    void stop();

private:
    struct ActiveRequest {
        TtsCallbacks callbacks;
        AudioFormat audio_format;
    };

    struct OutboundCommand {
        RequestId request_id = 0;
        ControlMessage message;
    };

    RequestId allocate_request_id_locked(RequestId requested_id);
    bool enqueue_outbound_locked(OutboundCommand command);
    void writer_loop();
    void dispatcher_loop();
    void handle_event(WorkerSessionEvent event);
    void handle_control_event(const WorkerSessionEvent& event);
    void handle_audio_event(WorkerSessionEvent event);
    void handle_worker_error(const WorkerSessionEvent& event);
    void handle_local_error(
        const WorkerSessionEvent& event,
        const std::string& category,
        const std::string& code);
    void complete_request(RequestId request_id);
    void cancel_request_locally(RequestId request_id);
    void fail_request(RequestId request_id, TtsError error);
    void fail_all_requests(TtsError error);
    void join_threads();

    mutable std::mutex mutex_;
    std::condition_variable outbound_condition_;
    std::unique_ptr<WorkerSession> session_;
    QwenTtsClientOptions options_;
    std::unordered_map<RequestId, ActiveRequest> active_requests_;
    std::deque<OutboundCommand> outbound_queue_;
    std::thread writer_thread_;
    std::thread dispatcher_thread_;
    RequestId next_request_id_ = 1;
    bool running_ = false;
    bool stopping_ = false;
};

} // namespace qwen_tts_bridge
