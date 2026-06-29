#include <qwen_tts_bridge/session/WorkerSession.hpp>

#include <algorithm>
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
        if (transport_ == nullptr || started_ || options_.max_event_queue_events == 0) {
            return false;
        }
        started_ = true;
        stopping_ = false;
        exited_ = false;
        ready_ = false;
        event_queue_overflowed_ = false;
        parser_.clear();
        events_.clear();
        queued_event_bytes_ = 0;
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
        started_ = false;
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
        return ready_ || exited_ || event_queue_overflowed_ || stopping_;
    });

    if (!ready || !ready_) {
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
        if (!started_ || stopping_ || transport_ == nullptr || !transport_->is_running()) {
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
    if (!ready_) {
        return false;
    }
    ready = ready_message_;
    return true;
}

bool WorkerSession::is_ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ready_;
}

bool WorkerSession::is_running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return transport_ != nullptr && transport_->is_running();
}

void WorkerSession::stop() {
    ITransport* transport = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
        transport = transport_.get();
    }

    if (transport != nullptr && transport->is_running()) {
        ShutdownMessage shutdown;
        send_control_frame(0, ControlMessage{shutdown});
        transport->stop();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    started_ = false;
    ready_ = false;
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
        enqueue_event_locked(make_session_error(payload.diagnostic), true);
        notify_state_locked();
        return false;
    }

    const EncodeResult encoded = encode_frame(
        FrameType::ControlJson,
        request_id,
        payload.payload);
    if (!encoded) {
        std::lock_guard<std::mutex> lock(mutex_);
        enqueue_event_locked(make_session_error(encoded.message), true);
        notify_state_locked();
        return false;
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
                enqueue_event_locked(
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
    enqueue_event_locked(std::move(event), true);
    notify_state_locked();
}

void WorkerSession::handle_exit(int exit_status) {
    std::lock_guard<std::mutex> lock(mutex_);
    exited_ = true;
    WorkerSessionEvent event;
    event.type = WorkerSessionEventType::Exited;
    event.exit_status = exit_status;
    enqueue_event_locked(std::move(event), true);
    notify_state_locked();
}

bool WorkerSession::handle_frame_locked(Frame frame) {
    WorkerSessionEvent event;
    event.request_id = frame.header.request_id;

    if (frame.header.frame_type == FrameType::ControlJson) {
        const ControlDecodeResult decoded = decode_control_message(
            frame.payload,
            ControlMessageDirection::WorkerToClient);
        if (!decoded) {
            return enqueue_event_locked(
                make_protocol_error(decoded.diagnostic),
                true);
        }

        if (control_message_type(decoded.message) == ControlMessageType::Ready) {
            ready_ = true;
            ready_message_ = std::get<ReadyMessage>(decoded.message);
        }

        event.type = WorkerSessionEventType::Control;
        event.control = decoded.message;
        return enqueue_event_locked(std::move(event));
    }

    if (frame.header.frame_type == FrameType::AudioPcm) {
        event.type = WorkerSessionEventType::Audio;
        event.audio = std::move(frame.payload);
        return enqueue_event_locked(std::move(event));
    }

    if (frame.header.frame_type == FrameType::ErrorJson) {
        const ErrorDecodeResult decoded = decode_error_message(frame.payload);
        if (!decoded) {
            return enqueue_event_locked(
                make_protocol_error(decoded.diagnostic),
                true);
        }

        event.type = WorkerSessionEventType::WorkerError;
        event.error = decoded.message;
        return enqueue_event_locked(std::move(event));
    }

    return enqueue_event_locked(make_protocol_error("unknown frame type"), true);
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
        event.message.size() +
        event.error.category.size() +
        event.error.code.size() +
        event.error.message.size();
}

void WorkerSession::notify_state_locked() {
    condition_.notify_all();
}

} // namespace qwen_tts_bridge
