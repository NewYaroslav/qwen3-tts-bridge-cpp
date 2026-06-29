#include <qwen_tts_bridge/session/WorkerSession.hpp>

#include <algorithm>
#include <type_traits>
#include <utility>
#include <variant>

namespace qwen_tts_bridge {
namespace {

WorkerSessionEvent make_session_error(std::string message) {
    WorkerSessionEvent event;
    event.type = WorkerSessionEventType::SessionError;
    event.message = std::move(message);
    return event;
}

WorkerSessionEvent make_protocol_error(std::string message) {
    WorkerSessionEvent event;
    event.type = WorkerSessionEventType::ProtocolError;
    event.message = std::move(message);
    return event;
}

bool is_client_to_worker_control(ControlMessageType message_type) {
    return message_type == ControlMessageType::Hello ||
        message_type == ControlMessageType::Synthesize ||
        message_type == ControlMessageType::Cancel ||
        message_type == ControlMessageType::Ping ||
        message_type == ControlMessageType::Shutdown;
}

bool is_session_level_worker_control(ControlMessageType message_type) {
    return message_type == ControlMessageType::Ready ||
        message_type == ControlMessageType::Pong ||
        message_type == ControlMessageType::ShutdownAck;
}

bool is_request_level_worker_control(ControlMessageType message_type) {
    return message_type == ControlMessageType::Queued ||
        message_type == ControlMessageType::Started ||
        message_type == ControlMessageType::Completed ||
        message_type == ControlMessageType::Cancelled;
}

} // namespace

WorkerSession::WorkerSession(
    std::unique_ptr<ITransport> transport,
    WorkerSessionOptions options)
    : transport_(std::move(transport)),
      options_(std::move(options)) {}

WorkerSession::~WorkerSession() {
    stop();
}

bool WorkerSession::start() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (transport_ == nullptr ||
            start_attempted_ ||
            state_ != WorkerSessionState::Stopped ||
            options_.max_event_queue_events == 0 ||
            options_.max_event_queue_bytes == 0) {
            return false;
        }
        start_attempted_ = true;
        state_ = WorkerSessionState::Starting;
        event_queue_overflowed_ = false;
        parser_.clear();
        events_.clear();
        queued_event_bytes_ = 0;
        shutdown_requested_ = false;
    }

    const bool transport_started = transport_->start(
        [this](ITransport::Bytes bytes) {
            handle_bytes(std::move(bytes));
        },
        [this](std::string message) {
            handle_transport_error(std::move(message));
        },
        [this](int exit_status) {
            handle_exit(exit_status);
        });

    if (!transport_started) {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = WorkerSessionState::Failed;
        notify_state_locked();
        return false;
    }

    bool should_send_hello = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        should_send_hello = state_ == WorkerSessionState::Starting;
    }
    if (!should_send_hello) {
        // A transport callback may have reported a startup failure while
        // start() was still returning from transport startup.
        stop();
        return false;
    }

    HelloMessage hello;
    hello.client_name = options_.client_name;
    hello.client_version = options_.client_version;
    if (!send_control_frame(0, ControlMessage{hello})) {
        stop();
        return false;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    const bool ready = condition_.wait_for(lock, options_.startup_timeout, [this]() {
        return state_ == WorkerSessionState::Ready ||
            state_ == WorkerSessionState::Failed ||
            state_ == WorkerSessionState::Exited ||
            state_ == WorkerSessionState::Stopping;
    });

    const bool success =
        ready &&
        state_ == WorkerSessionState::Ready &&
        !event_queue_overflowed_ &&
        transport_ != nullptr &&
        transport_->is_running();

    if (!success) {
        lock.unlock();
        stop();
        return false;
    }

    return true;
}

bool WorkerSession::send_control(
    RequestId request_id,
    const ControlMessage& message) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != WorkerSessionState::Ready ||
            transport_ == nullptr ||
            !transport_->is_running()) {
            return false;
        }
    }

    return send_control_frame(request_id, message);
}

bool WorkerSession::wait_for_event(
    WorkerSessionEvent& event,
    std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    const bool available = condition_.wait_for(lock, timeout, [this]() {
        return !events_.empty();
    });
    if (!available) {
        return false;
    }

    event = std::move(events_.front());
    queued_event_bytes_ -= std::min(
        queued_event_bytes_,
        event_payload_size(event));
    events_.pop_front();
    return true;
}

bool WorkerSession::ready_message(ReadyMessage& ready) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != WorkerSessionState::Ready) {
        return false;
    }
    ready = ready_message_;
    return true;
}

bool WorkerSession::is_ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_ == WorkerSessionState::Ready;
}

WorkerSessionState WorkerSession::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

bool WorkerSession::is_running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return transport_ != nullptr && transport_->is_running();
}

void WorkerSession::stop() {
    ITransport* transport = nullptr;
    bool send_shutdown = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == WorkerSessionState::Stopped ||
            state_ == WorkerSessionState::Stopping) {
            return;
        }
        send_shutdown = state_ == WorkerSessionState::Ready;
        if (state_ != WorkerSessionState::Exited) {
            state_ = WorkerSessionState::Stopping;
        }
        transport = transport_.get();
        notify_state_locked();
    }

    if (transport != nullptr && transport->is_running()) {
        if (send_shutdown) {
            ShutdownMessage shutdown;
            send_control_frame(0, ControlMessage{shutdown});
        }
        transport->stop();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == WorkerSessionState::Stopping) {
        state_ = WorkerSessionState::Stopped;
    }
    condition_.notify_all();
}

bool WorkerSession::send_control_frame(
    RequestId request_id,
    const ControlMessage& message) {
    if (transport_ == nullptr) {
        return false;
    }

    if (!is_client_to_worker_control(control_message_type(message))) {
        std::lock_guard<std::mutex> lock(mutex_);
        enqueue_event_locked(
            make_session_error("cannot send worker-to-client control message"),
            true);
        notify_state_locked();
        return false;
    }

    const JsonPayloadEncodeResult payload = encode_control_message(message);
    if (!payload) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == WorkerSessionState::Starting) {
            fail_with_event_locked(make_session_error(payload.diagnostic), true);
        }
        else {
            enqueue_event_locked(make_session_error(payload.diagnostic), true);
        }
        notify_state_locked();
        return false;
    }

    const EncodeResult encoded = encode_frame(
        FrameType::ControlJson,
        request_id,
        payload.payload);
    if (!encoded) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == WorkerSessionState::Starting) {
            fail_with_event_locked(make_session_error(encoded.message), true);
        }
        else {
            enqueue_event_locked(make_session_error(encoded.message), true);
        }
        notify_state_locked();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        mark_outbound_control_locked(request_id, control_message_type(message));
    }

    return transport_->send(encoded.bytes.data(), encoded.bytes.size());
}

void WorkerSession::handle_bytes(ITransport::Bytes bytes) {
    bool stop_transport = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        parser_.append(bytes);

        while (true) {
            ParseResult parsed = parser_.parse_next();
            if (parsed.status == ParseStatus::NeedMoreData) {
                break;
            }

            if (parsed.status == ParseStatus::FatalError) {
                fail_with_event_locked(
                    make_protocol_error(parsed.message),
                    true);
                stop_transport = true;
                break;
            }

            stop_transport = handle_frame_locked(std::move(parsed.frame)) || stop_transport;
            if (stop_transport) {
                break;
            }
        }

        notify_state_locked();
    }

    if (stop_transport && transport_ != nullptr) {
        transport_->stop();
    }
}

void WorkerSession::handle_transport_error(std::string message) {
    std::lock_guard<std::mutex> lock(mutex_);
    WorkerSessionEvent event;
    event.type = WorkerSessionEventType::TransportError;
    event.message = std::move(message);
    if (state_ == WorkerSessionState::Starting ||
        state_ == WorkerSessionState::Ready) {
        fail_with_event_locked(std::move(event), true);
    }
    else {
        enqueue_event_locked(std::move(event), true);
    }
    notify_state_locked();
}

void WorkerSession::handle_exit(int exit_status) {
    std::lock_guard<std::mutex> lock(mutex_);
    WorkerSessionEvent event;
    event.type = WorkerSessionEventType::Exited;
    event.exit_status = exit_status;
    enqueue_event_locked(std::move(event), true);
    state_ = WorkerSessionState::Exited;
    notify_state_locked();
}

bool WorkerSession::handle_frame_locked(Frame frame) {
    if (frame.header.frame_type == FrameType::ControlJson) {
        return handle_control_frame_locked(std::move(frame));
    }

    if (frame.header.frame_type == FrameType::AudioPcm) {
        return handle_audio_frame_locked(std::move(frame));
    }

    if (frame.header.frame_type == FrameType::ErrorJson) {
        return handle_error_frame_locked(std::move(frame));
    }

    return fail_with_event_locked(make_protocol_error("unknown frame type"), true);
}

bool WorkerSession::handle_control_frame_locked(Frame frame) {
    const ControlDecodeResult decoded = decode_control_message(
        frame.payload,
        ControlMessageDirection::WorkerToClient);
    if (!decoded) {
        return fail_with_event_locked(make_protocol_error(decoded.diagnostic), true);
    }

    const ControlMessageType message_type = control_message_type(decoded.message);
    if (!is_control_message_allowed_locked(frame.header.request_id, message_type)) {
        return fail_with_event_locked(
            make_protocol_error("control message is invalid in the current session state"),
            true);
    }

    WorkerSessionEvent event;
    event.type = WorkerSessionEventType::Control;
    event.request_id = frame.header.request_id;
    event.control = decoded.message;

    if (message_type == ControlMessageType::Ready) {
        const bool should_stop = enqueue_event_locked(std::move(event));
        if (should_stop) {
            return true;
        }
        ready_message_ = std::get<ReadyMessage>(decoded.message);
        state_ = WorkerSessionState::Ready;
        return false;
    }

    return enqueue_event_locked(std::move(event));
}

bool WorkerSession::handle_audio_frame_locked(Frame frame) {
    if (state_ != WorkerSessionState::Ready || frame.header.request_id == 0) {
        return fail_with_event_locked(
            make_protocol_error("audio frame is invalid in the current session state"),
            true);
    }

    WorkerSessionEvent event;
    event.type = WorkerSessionEventType::Audio;
    event.request_id = frame.header.request_id;
    event.audio = std::move(frame.payload);
    return enqueue_event_locked(std::move(event));
}

bool WorkerSession::handle_error_frame_locked(Frame frame) {
    const ErrorDecodeResult decoded = decode_error_message(frame.payload);
    if (!decoded) {
        return fail_with_event_locked(make_protocol_error(decoded.diagnostic), true);
    }

    WorkerSessionEvent event;
    event.type = WorkerSessionEventType::WorkerError;
    event.request_id = frame.header.request_id;
    event.error = decoded.message;

    if (state_ == WorkerSessionState::Starting) {
        return fail_with_event_locked(std::move(event));
    }

    return enqueue_event_locked(std::move(event));
}

bool WorkerSession::enqueue_event_locked(
    WorkerSessionEvent event,
    bool exempt_from_limits) {
    const std::size_t payload_size = event_payload_size(event);
    if (!exempt_from_limits && !event_queue_overflowed_) {
        const bool event_count_full = events_.size() >= options_.max_event_queue_events;
        const bool byte_count_full =
            payload_size > options_.max_event_queue_bytes ||
            queued_event_bytes_ > options_.max_event_queue_bytes - payload_size;

        if (event_count_full || byte_count_full) {
            event_queue_overflowed_ = true;
            WorkerSessionEvent overflow = make_session_error(
                "worker session event queue overflow");
            if (state_ == WorkerSessionState::Starting ||
                state_ == WorkerSessionState::Ready) {
                state_ = WorkerSessionState::Failed;
            }
            queued_event_bytes_ += event_payload_size(overflow);
            events_.push_back(std::move(overflow));
            return true;
        }
    }

    if (event_queue_overflowed_ && !exempt_from_limits) {
        return true;
    }

    queued_event_bytes_ += payload_size;
    events_.push_back(std::move(event));
    return false;
}

std::size_t WorkerSession::event_payload_size(const WorkerSessionEvent& event) const {
    return event.audio.size() +
        control_payload_size(event.control) +
        event.message.size() +
        event.error.category.size() +
        event.error.code.size() +
        event.error.message.size();
}

std::size_t WorkerSession::control_payload_size(const ControlMessage& message) const {
    return std::visit(
        [](const auto& value) -> std::size_t {
            using Message = std::decay_t<decltype(value)>;

            if constexpr (std::is_same_v<Message, HelloMessage>) {
                return value.client_name.size() + value.client_version.size();
            }
            else if constexpr (std::is_same_v<Message, SynthesizeMessage>) {
                return value.text.size() +
                    value.language.size() +
                    value.speaker.size() +
                    value.instruction.size() +
                    value.output.sample_format.size();
            }
            else if constexpr (std::is_same_v<Message, ShutdownMessage>) {
                return value.mode.size();
            }
            else if constexpr (std::is_same_v<Message, ReadyMessage>) {
                return value.worker_version.size() + value.session_id.size();
            }
            else if constexpr (std::is_same_v<Message, StartedMessage>) {
                return value.audio_format.sample_format.size();
            }
            else {
                return 0;
            }
        },
        message);
}

bool WorkerSession::fail_with_event_locked(
    WorkerSessionEvent event,
    bool exempt_from_limits) {
    if (state_ == WorkerSessionState::Starting ||
        state_ == WorkerSessionState::Ready) {
        state_ = WorkerSessionState::Failed;
    }
    return enqueue_event_locked(std::move(event), exempt_from_limits);
}

void WorkerSession::mark_outbound_control_locked(
    RequestId request_id,
    ControlMessageType message_type) {
    if (request_id == 0 && message_type == ControlMessageType::Shutdown) {
        shutdown_requested_ = true;
    }
}

bool WorkerSession::is_ready_message_allowed_locked(RequestId request_id) const {
    return state_ == WorkerSessionState::Starting && request_id == 0;
}

bool WorkerSession::is_control_message_allowed_locked(
    RequestId request_id,
    ControlMessageType message_type) const {
    if (message_type == ControlMessageType::Ready) {
        return is_ready_message_allowed_locked(request_id);
    }

    if (state_ != WorkerSessionState::Ready) {
        return false;
    }

    if (message_type == ControlMessageType::ShutdownAck) {
        return request_id == 0 && shutdown_requested_;
    }

    if (is_session_level_worker_control(message_type)) {
        return request_id == 0;
    }
    if (is_request_level_worker_control(message_type)) {
        return request_id != 0;
    }

    return false;
}

void WorkerSession::notify_state_locked() {
    condition_.notify_all();
}

} // namespace qwen_tts_bridge
