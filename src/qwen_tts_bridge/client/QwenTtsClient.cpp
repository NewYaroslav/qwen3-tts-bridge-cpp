#include <qwen_tts_bridge/client/QwenTtsClient.hpp>

#include <algorithm>
#include <chrono>
#include <exception>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace qwen_tts_bridge {
namespace {

constexpr std::size_t outbound_command_fixed_overhead = 64u;

TtsError make_local_error(
    RequestId request_id,
    std::string category,
    std::string code,
    std::string message) {
    TtsError error;
    error.request_id = request_id;
    error.category = std::move(category);
    error.code = std::move(code);
    error.message = std::move(message);
    return error;
}

SynthesizeMessage to_control_message(const TtsRequest& request) {
    SynthesizeMessage message;
    message.text = request.text;
    message.language = request.language;
    message.speaker = request.speaker;
    message.instruction = request.instruction;
    message.output = request.output;
    return message;
}

} // namespace

QwenTtsClient::QwenTtsClient() = default;

QwenTtsClient::~QwenTtsClient() {
    stop();
}

bool QwenTtsClient::start(
    std::unique_ptr<ITransport> transport,
    QwenTtsClientOptions options) {
    if (transport == nullptr ||
        options.max_outbound_commands == 0 ||
        options.max_outbound_command_bytes == 0) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_ || stopping_) {
            return false;
        }
    }
    reap_finished_threads();

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (running_ ||
            stopping_ ||
            session_ != nullptr ||
            writer_thread_.joinable() ||
            dispatcher_thread_.joinable()) {
            return false;
        }
    }

    auto session = std::make_unique<WorkerSession>(
        std::move(transport),
        options.session);
    if (!session->start()) {
        session->stop();
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        options_ = std::move(options);
        session_ = std::move(session);
        active_requests_.clear();
        clear_outbound_queue_locked();
        next_request_id_ = 1;
        running_ = true;
        stopping_ = false;
        terminal_failure_handled_ = false;
    }

    try {
        writer_thread_ = std::thread(&QwenTtsClient::writer_loop, this);
        dispatcher_thread_ = std::thread(&QwenTtsClient::dispatcher_loop, this);
    }
    catch (...) {
        stop();
        return false;
    }

    return true;
}

bool QwenTtsClient::start(
    const StdIoTransportOptions& transport_options,
    QwenTtsClientOptions options) {
    return start(
        std::make_unique<StdIoTransport>(transport_options),
        std::move(options));
}

bool QwenTtsClient::start(const std::string& worker_executable) {
    StdIoTransportOptions transport_options;
    transport_options.arguments.push_back(worker_executable);
    return start(transport_options);
}

RequestId QwenTtsClient::synthesize_async(
    TtsRequest request,
    TtsCallbacks callbacks) {
    if (request.text.empty()) {
        return 0;
    }

    SynthesizeMessage message = to_control_message(request);
    ControlMessage control_message{message};
    if (!encode_control_message(control_message)) {
        return 0;
    }

    ActiveRequest active_request;
    active_request.callbacks = std::move(callbacks);
    active_request.audio_format = request.output;

    RequestId request_id = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ || stopping_ || session_ == nullptr) {
            return 0;
        }

        request_id = allocate_request_id_locked(request.id);
        if (request_id == 0) {
            return 0;
        }

        OutboundCommand command;
        command.request_id = request_id;
        command.message = std::move(control_message);

        active_requests_.emplace(request_id, std::move(active_request));
        if (!enqueue_outbound_locked(std::move(command))) {
            active_requests_.erase(request_id);
            return 0;
        }
    }

    outbound_condition_.notify_one();
    return request_id;
}

RequestId QwenTtsClient::synthesize_async(
    const std::string& text,
    TtsCallbacks callbacks) {
    TtsRequest request;
    request.text = text;
    return synthesize_async(std::move(request), std::move(callbacks));
}

bool QwenTtsClient::cancel(RequestId request_id) {
    if (request_id == 0) {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_ ||
            stopping_ ||
            session_ == nullptr ||
            active_requests_.find(request_id) == active_requests_.end()) {
            return false;
        }

        OutboundCommand command;
        command.request_id = request_id;
        command.message = ControlMessage{CancelMessage{}};
        if (!enqueue_outbound_locked(std::move(command))) {
            return false;
        }
    }

    outbound_condition_.notify_one();
    return true;
}

bool QwenTtsClient::is_running() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return running_ && !stopping_ && session_ != nullptr;
}

void QwenTtsClient::stop() {
    WorkerSession* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        stopping_ = true;
        clear_outbound_queue_locked();
        session = session_.get();
    }
    outbound_condition_.notify_all();

    if (session != nullptr) {
        session->stop();
    }

    join_threads();

    fail_all_requests(make_local_error(
        0,
        "client_error",
        "client_stopped",
        "QwenTtsClient stopped before request reached a terminal event"));

    const bool called_from_dispatcher = is_current_dispatcher_thread();
    std::lock_guard<std::mutex> lock(mutex_);
    if (!writer_thread_.joinable() &&
        (!dispatcher_thread_.joinable() || called_from_dispatcher)) {
        session_.reset();
        clear_outbound_queue_locked();
        stopping_ = false;
        running_ = false;
        terminal_failure_handled_ = false;
    }
}

RequestId QwenTtsClient::allocate_request_id_locked(RequestId requested_id) {
    if (requested_id != 0) {
        if (active_requests_.find(requested_id) != active_requests_.end()) {
            return 0;
        }
        if (requested_id >= next_request_id_) {
            next_request_id_ = requested_id + 1;
            if (next_request_id_ == 0) {
                next_request_id_ = 1;
            }
        }
        return requested_id;
    }

    const RequestId first_candidate = next_request_id_;
    do {
        const RequestId candidate = next_request_id_;
        ++next_request_id_;
        if (next_request_id_ == 0) {
            next_request_id_ = 1;
        }
        if (active_requests_.find(candidate) == active_requests_.end()) {
            return candidate;
        }
    } while (next_request_id_ != first_candidate);

    return 0;
}

bool QwenTtsClient::enqueue_outbound_locked(OutboundCommand command) {
    const std::size_t command_bytes = outbound_command_size(command);
    if (outbound_queue_.size() >= options_.max_outbound_commands) {
        return false;
    }
    if (command_bytes > options_.max_outbound_command_bytes ||
        queued_outbound_bytes_ >
            options_.max_outbound_command_bytes - command_bytes) {
        return false;
    }

    command.queued_bytes = command_bytes;
    queued_outbound_bytes_ += command_bytes;
    outbound_queue_.push_back(std::move(command));
    return true;
}

void QwenTtsClient::clear_outbound_queue_locked() {
    outbound_queue_.clear();
    queued_outbound_bytes_ = 0;
}

std::size_t QwenTtsClient::outbound_command_size(
    const OutboundCommand& command) const {
    return std::visit(
        [](const auto& message) -> std::size_t {
            using Message = std::decay_t<decltype(message)>;

            if constexpr (std::is_same_v<Message, HelloMessage>) {
                return outbound_command_fixed_overhead +
                    message.client_name.size() +
                    message.client_version.size();
            }
            else if constexpr (std::is_same_v<Message, SynthesizeMessage>) {
                return outbound_command_fixed_overhead +
                    message.text.size() +
                    message.language.size() +
                    message.speaker.size() +
                    message.instruction.size() +
                    message.output.sample_format.size();
            }
            else if constexpr (std::is_same_v<Message, ShutdownMessage>) {
                return outbound_command_fixed_overhead + message.mode.size();
            }
            else {
                return outbound_command_fixed_overhead;
            }
        },
        command.message);
}

void QwenTtsClient::reap_finished_threads() {
    join_threads();

    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ &&
        !stopping_ &&
        !writer_thread_.joinable() &&
        !dispatcher_thread_.joinable()) {
        session_.reset();
        clear_outbound_queue_locked();
        terminal_failure_handled_ = false;
    }
}

bool QwenTtsClient::is_current_dispatcher_thread() const {
    return dispatcher_thread_.joinable() &&
        dispatcher_thread_.get_id() == std::this_thread::get_id();
}

void QwenTtsClient::writer_loop() {
    while (true) {
        OutboundCommand command;
        WorkerSession* session = nullptr;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            outbound_condition_.wait(lock, [this]() {
                return stopping_ || !outbound_queue_.empty();
            });

            if (outbound_queue_.empty()) {
                if (stopping_) {
                    break;
                }
                continue;
            }

            command = std::move(outbound_queue_.front());
            queued_outbound_bytes_ -= std::min(
                queued_outbound_bytes_,
                command.queued_bytes);
            outbound_queue_.pop_front();
            session = session_.get();
        }

        if (session == nullptr) {
            continue;
        }

        session->send_control(command.request_id, command.message);
    }
}

void QwenTtsClient::dispatcher_loop() {
    while (true) {
        WorkerSession* session = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            session = session_.get();
        }

        if (session == nullptr) {
            break;
        }

        WorkerSessionEvent event;
        if (session->wait_for_event(event, std::chrono::milliseconds(100))) {
            handle_event(std::move(event));
            continue;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            break;
        }
    }
}

void QwenTtsClient::handle_event(WorkerSessionEvent event) {
    switch (event.type) {
    case WorkerSessionEventType::Control:
        handle_control_event(event);
        break;
    case WorkerSessionEventType::Audio:
        handle_audio_event(std::move(event));
        break;
    case WorkerSessionEventType::WorkerError:
        handle_worker_error(event);
        break;
    case WorkerSessionEventType::TransportError:
        handle_local_error(event, "transport_error", "transport_error");
        break;
    case WorkerSessionEventType::ProtocolError:
        handle_local_error(event, "protocol_error", "protocol_error");
        break;
    case WorkerSessionEventType::SessionError:
        handle_local_error(event, "session_error", "session_error");
        break;
    case WorkerSessionEventType::Exited:
        handle_local_error(event, "transport_error", "worker_exited");
        break;
    }
}

void QwenTtsClient::handle_control_event(const WorkerSessionEvent& event) {
    switch (control_message_type(event.control)) {
    case ControlMessageType::Started: {
        const auto& started = std::get<StartedMessage>(event.control);
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_requests_.find(event.request_id);
        if (it != active_requests_.end()) {
            it->second.audio_format = started.audio_format;
        }
        break;
    }
    case ControlMessageType::Completed:
        complete_request(event.request_id);
        break;
    case ControlMessageType::Cancelled:
        cancel_request_locally(event.request_id);
        break;
    case ControlMessageType::Ready:
    case ControlMessageType::Queued:
    case ControlMessageType::Pong:
    case ControlMessageType::ShutdownAck:
    case ControlMessageType::Hello:
    case ControlMessageType::Synthesize:
    case ControlMessageType::Cancel:
    case ControlMessageType::Ping:
    case ControlMessageType::Shutdown:
        break;
    }
}

void QwenTtsClient::handle_audio_event(WorkerSessionEvent event) {
    TtsCallbacks callbacks;
    AudioFormat format;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_requests_.find(event.request_id);
        if (it == active_requests_.end()) {
            return;
        }
        callbacks = it->second.callbacks;
        format = it->second.audio_format;
    }

    if (callbacks.on_audio) {
        PcmChunk chunk;
        chunk.request_id = event.request_id;
        chunk.format = std::move(format);
        chunk.bytes = std::move(event.audio);
        invoke_user_callback([&callbacks, &chunk]() {
            callbacks.on_audio(chunk);
        });
    }
}

void QwenTtsClient::handle_worker_error(const WorkerSessionEvent& event) {
    TtsError error;
    error.request_id = event.request_id;
    error.category = event.error.category;
    error.code = event.error.code;
    error.message = event.error.message;

    if (event.request_id == 0) {
        fail_all_requests(std::move(error));
    }
    else {
        fail_request(event.request_id, std::move(error));
    }
}

void QwenTtsClient::handle_local_error(
    const WorkerSessionEvent& event,
    const std::string& category,
    const std::string& code) {
    WorkerSession* session = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (terminal_failure_handled_) {
            return;
        }
        terminal_failure_handled_ = true;
        running_ = false;
        stopping_ = true;
        clear_outbound_queue_locked();
        session = session_.get();
    }
    outbound_condition_.notify_all();

    if (session != nullptr) {
        session->stop();
    }

    const std::string message = event.message.empty()
        ? code
        : event.message;

    fail_all_requests(make_local_error(
        event.request_id,
        category,
        code,
        message));
}

void QwenTtsClient::complete_request(RequestId request_id) {
    TtsCallbacks callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_requests_.find(request_id);
        if (it == active_requests_.end()) {
            return;
        }
        callbacks = std::move(it->second.callbacks);
        active_requests_.erase(it);
    }

    if (callbacks.on_completed) {
        invoke_user_callback(callbacks.on_completed);
    }
}

void QwenTtsClient::cancel_request_locally(RequestId request_id) {
    TtsCallbacks callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_requests_.find(request_id);
        if (it == active_requests_.end()) {
            return;
        }
        callbacks = std::move(it->second.callbacks);
        active_requests_.erase(it);
    }

    if (callbacks.on_cancelled) {
        invoke_user_callback(callbacks.on_cancelled);
    }
}

void QwenTtsClient::fail_request(RequestId request_id, TtsError error) {
    TtsCallbacks callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = active_requests_.find(request_id);
        if (it == active_requests_.end()) {
            return;
        }
        callbacks = std::move(it->second.callbacks);
        active_requests_.erase(it);
    }

    if (callbacks.on_error) {
        invoke_user_callback([&callbacks, &error]() {
            callbacks.on_error(error);
        });
    }
}

void QwenTtsClient::fail_all_requests(TtsError error) {
    std::vector<std::pair<RequestId, TtsCallbacks>> callbacks;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        callbacks.reserve(active_requests_.size());
        for (auto& item : active_requests_) {
            callbacks.emplace_back(item.first, std::move(item.second.callbacks));
        }
        active_requests_.clear();
    }

    for (auto& item : callbacks) {
        auto& callback_set = item.second;
        if (callback_set.on_error) {
            TtsError request_error = error;
            if (request_error.request_id == 0) {
                request_error.request_id = item.first;
            }
            invoke_user_callback([&callback_set, &request_error]() {
                callback_set.on_error(request_error);
            });
        }
    }
}

void QwenTtsClient::invoke_user_callback(
    const std::function<void()>& callback) noexcept {
    if (!callback) {
        return;
    }

    try {
        callback();
    }
    catch (...) {
        report_callback_exception(std::current_exception());
    }
}

void QwenTtsClient::report_callback_exception(
    std::exception_ptr exception) noexcept {
    std::function<void(std::exception_ptr)> handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        handler = options_.on_callback_exception;
    }

    if (!handler) {
        return;
    }

    try {
        handler(std::move(exception));
    }
    catch (...) {
    }
}

void QwenTtsClient::join_threads() {
    const auto current_thread = std::this_thread::get_id();

    if (writer_thread_.joinable() &&
        writer_thread_.get_id() != current_thread) {
        writer_thread_.join();
    }

    if (dispatcher_thread_.joinable() &&
        dispatcher_thread_.get_id() != current_thread) {
        dispatcher_thread_.join();
    }
}

} // namespace qwen_tts_bridge
