#pragma once

/// \file WorkerSession.hpp
/// \brief Worker lifetime and protocol event orchestration.

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <qwen_tts_bridge/data.hpp>
#include <qwen_tts_bridge/protocol/control.hpp>
#include <qwen_tts_bridge/protocol/framing.hpp>
#include <qwen_tts_bridge/transport/ITransport.hpp>

namespace qwen_tts_bridge {

/// \struct WorkerSessionOptions
/// \brief Runtime options for the C++ side worker session.
struct WorkerSessionOptions {
    /// \brief Name sent in the hello control message.
    std::string client_name = "qwen-tts-bridge-cpp";

    /// \brief Version sent in the hello control message.
    std::string client_version = "0.2.0";

    /// \brief Maximum time start() waits for worker ready.
    std::chrono::milliseconds startup_timeout{10000};

    /// \brief Maximum regular events waiting for application dispatch.
    ///
    /// This is a defensive memory bound for the session event queue. The
    /// internally generated overflow diagnostic and final exit event are
    /// exempt so failure and process termination remain observable.
    std::size_t max_event_queue_events = 4096u;

    /// \brief Maximum regular event payload bytes waiting in the session queue.
    ///
    /// Counts audio payload bytes and diagnostic string bytes. Control events
    /// are bounded by frame/control payload limits before they reach this queue.
    std::size_t max_event_queue_bytes = 16u * 1024u * 1024u;
};

/// \enum WorkerSessionEventType
/// \brief Event kinds emitted by WorkerSession.
enum class WorkerSessionEventType {
    Control,        ///< Decoded worker-to-client control message.
    Audio,          ///< Raw PCM audio frame.
    WorkerError,    ///< Decoded worker error_json frame.
    TransportError, ///< Local transport error.
    ProtocolError,  ///< Framing or JSON protocol error.
    SessionError,   ///< Local session resource or state error.
    Exited          ///< Worker process or transport peer exited.
};

/// \enum WorkerSessionState
/// \brief Coarse worker session lifecycle state.
enum class WorkerSessionState {
    Stopped,  ///< Session has not been started yet.
    Starting, ///< Transport started and ready handshake is pending.
    Ready,    ///< Worker completed the ready handshake.
    Stopping, ///< Stop has been requested.
    Failed,   ///< Local, transport, or protocol failure occurred.
    Exited    ///< Worker process or transport peer exited.
};

/// \struct WorkerSessionEvent
/// \brief Event emitted by WorkerSession after transport and protocol parsing.
struct WorkerSessionEvent {
    WorkerSessionEventType type = WorkerSessionEventType::SessionError;
    RequestId request_id = 0;
    ControlMessage control;
    ErrorMessage error;
    std::vector<std::byte> audio;
    int exit_status = 0;
    std::string message;
};

/// \class WorkerSession
/// \brief Owns a worker transport and converts protocol frames into events.
///
/// WorkerSession is an internal orchestration layer for the future public
/// client. It does not invoke user synthesis callbacks directly. Instead, it
/// exposes a bounded event queue that a higher-level dispatcher can consume.
class WorkerSession final {
public:
    /// \brief Creates a session around an already configured transport.
    /// \param transport Transport used to communicate with the worker.
    /// \param options Session options.
    WorkerSession(
        std::unique_ptr<ITransport> transport,
        WorkerSessionOptions options = {});

    /// \brief Stops the transport if it is still running.
    ~WorkerSession();

    WorkerSession(const WorkerSession&) = delete;
    WorkerSession& operator=(const WorkerSession&) = delete;

    /// \brief Starts the worker transport and waits for ready.
    ///
    /// This method sends the protocol `hello` message after the transport
    /// starts. It blocks only until `ready`, startup timeout, transport exit, or
    /// a local/protocol error.
    ///
    /// \return True when the worker reached ready.
    bool start();

    /// \brief Sends a control message to the worker.
    /// \param request_id Frame request identifier.
    /// \param message Client-to-worker control message.
    /// \return True when the encoded frame was accepted by the transport.
    bool send_control(RequestId request_id, const ControlMessage& message);

    /// \brief Waits for the next queued session event.
    /// \param event Output event.
    /// \param timeout Maximum time to wait.
    /// \return True when an event was returned.
    bool wait_for_event(
        WorkerSessionEvent& event,
        std::chrono::milliseconds timeout);

    /// \brief Returns a copy of the ready payload if the session is ready.
    /// \param ready Output ready message.
    /// \return True when ready data is available.
    bool ready_message(ReadyMessage& ready) const;

    /// \brief Returns whether the worker has completed the ready handshake.
    bool is_ready() const;

    /// \brief Returns the current coarse session lifecycle state.
    WorkerSessionState state() const;

    /// \brief Returns whether the underlying transport is running.
    bool is_running() const;

    /// \brief Sends graceful shutdown and stops the transport.
    ///
    /// This method is idempotent. It may block while the transport stops.
    void stop();

private:
    bool send_control_frame(RequestId request_id, const ControlMessage& message);
    void handle_bytes(ITransport::Bytes bytes);
    void handle_transport_error(std::string message);
    void handle_exit(int exit_status);

    bool handle_frame_locked(Frame frame);
    bool handle_control_frame_locked(Frame frame);
    bool handle_audio_frame_locked(Frame frame);
    bool handle_error_frame_locked(Frame frame);
    bool enqueue_event_locked(WorkerSessionEvent event, bool exempt_from_limits = false);
    std::size_t event_payload_size(const WorkerSessionEvent& event) const;
    std::size_t control_payload_size(const ControlMessage& message) const;
    bool fail_with_event_locked(WorkerSessionEvent event, bool exempt_from_limits = true);
    void mark_outbound_control_locked(RequestId request_id, ControlMessageType message_type);
    bool is_ready_message_allowed_locked(RequestId request_id) const;
    bool is_control_message_allowed_locked(
        RequestId request_id,
        ControlMessageType message_type) const;
    void notify_state_locked();

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::unique_ptr<ITransport> transport_;
    WorkerSessionOptions options_;
    FrameParser parser_;
    std::deque<WorkerSessionEvent> events_;
    std::size_t queued_event_bytes_ = 0;
    WorkerSessionState state_ = WorkerSessionState::Stopped;
    bool event_queue_overflowed_ = false;
    bool start_attempted_ = false;
    bool shutdown_requested_ = false;
    ReadyMessage ready_message_;
};

} // namespace qwen_tts_bridge
