#include <qwen_tts_bridge/transport/stdio/StdIoTransport.hpp>

#include <process.hpp>

#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#if defined(_WIN32) && defined(UNICODE)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace qwen_tts_bridge {
namespace {

enum class ProcessState {
    Stopped,
    Starting,
    Running,
    Stopping
};

enum class CallbackEventType {
    Receive,
    Error,
    Exit,
    Stderr
};

struct CallbackEvent {
    CallbackEventType type = CallbackEventType::Error;
    ITransport::Bytes bytes;
    std::string message;
    int exit_status = -1;
};

TinyProcessLib::Process::string_type to_process_string(const std::string& value) {
#if defined(_WIN32) && defined(UNICODE)
    if (value.empty()) {
        return {};
    }

    if (value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("process string is too large to convert from UTF-8");
    }

    const auto input_size = static_cast<int>(value.size());
    const int required_size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        input_size,
        nullptr,
        0);

    if (required_size <= 0) {
        throw std::invalid_argument("invalid UTF-8 process string");
    }

    TinyProcessLib::Process::string_type converted(
        static_cast<std::size_t>(required_size),
        L'\0');
    const int written_size = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        input_size,
        converted.data(),
        required_size);

    if (written_size != required_size) {
        throw std::invalid_argument("failed to convert process string from UTF-8");
    }

    return converted;
#else
    return value;
#endif
}

bool is_started_process_id(TinyProcessLib::Process::id_type id) {
#if defined(_WIN32)
    return id != 0;
#else
    return id > 0;
#endif
}

std::vector<TinyProcessLib::Process::string_type> to_process_arguments(
    const std::vector<std::string>& arguments) {
    std::vector<TinyProcessLib::Process::string_type> converted;
    converted.reserve(arguments.size());
    for (const auto& argument : arguments) {
        converted.push_back(to_process_string(argument));
    }
    return converted;
}

TinyProcessLib::Process::environment_type to_process_environment(
    const std::unordered_map<std::string, std::string>& environment) {
    TinyProcessLib::Process::environment_type converted;
    for (const auto& item : environment) {
        converted.emplace(to_process_string(item.first), to_process_string(item.second));
    }
    return converted;
}

} // namespace

class StdIoTransport::Impl {
public:
    explicit Impl(StdIoTransportOptions options)
        : options_(std::move(options)) {}

    ~Impl() {
        stop();
    }

    bool start(
        ReceiveHandler receive_handler,
        ErrorHandler error_handler,
        ExitHandler exit_handler) {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);

        if (options_.arguments.empty()) {
            call_error(error_handler, "stdio transport requires at least one process argument");
            return false;
        }

        if (is_callback_thread()) {
            call_error(error_handler, "stdio transport cannot be started from a transport callback");
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (state_ != ProcessState::Stopped || process_ != nullptr) {
                call_error(error_handler, "stdio transport is already started");
                return false;
            }
        }

        join_exit_thread_if_needed();
        request_callback_thread_stop();
        join_callback_thread_if_needed();

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            state_ = ProcessState::Starting;
            receive_handler_ = std::move(receive_handler);
            error_handler_ = std::move(error_handler);
            exit_handler_ = std::move(exit_handler);
            exited_ = false;
            exit_status_ = -1;
        }

        try {
            start_callback_thread();

            TinyProcessLib::Config config;
            config.buffer_size = options_.read_buffer_size;
            config.show_window = TinyProcessLib::Config::ShowWindow::hide;

            auto stdout_handler = [this](const char* bytes, std::size_t size) {
                handle_stdout(bytes, size);
            };
            auto stderr_handler = [this](const char* bytes, std::size_t size) {
                handle_stderr(bytes, size);
            };

            auto arguments = to_process_arguments(options_.arguments);
            const auto working_directory = to_process_string(options_.working_directory);

            std::shared_ptr<TinyProcessLib::Process> process;
            if (options_.environment.empty()) {
                process = std::make_shared<TinyProcessLib::Process>(
                    arguments,
                    working_directory,
                    stdout_handler,
                    stderr_handler,
                    true,
                    config);
            }
            else {
                auto environment = to_process_environment(options_.environment);
                process = std::make_shared<TinyProcessLib::Process>(
                    arguments,
                    working_directory,
                    environment,
                    stdout_handler,
                    stderr_handler,
                    true,
                    config);
            }

            if (!is_started_process_id(process->get_id())) {
                fail_start("failed to start stdio worker process");
                return false;
            }

            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                process_ = process;
                state_ = ProcessState::Running;
            }

            exit_thread_ = std::thread([this, process]() {
                watch_process_exit(std::move(process));
            });

            return true;
        }
        catch (const std::exception& exc) {
            fail_start(exc.what());
            return false;
        }
        catch (...) {
            fail_start("unknown stdio transport startup error");
            return false;
        }
    }

    bool send(const std::byte* data, std::size_t size) {
        if (data == nullptr && size != 0) {
            report_error("cannot send a non-zero byte count from a null pointer");
            return false;
        }

        if (size == 0) {
            return true;
        }

        std::shared_ptr<TinyProcessLib::Process> process;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (state_ == ProcessState::Running && !exited_) {
                process = process_;
            }
        }

        if (process == nullptr) {
            report_error("stdio transport is not running");
            return false;
        }

        bool ok = false;
        std::string write_error;

        {
            std::lock_guard<std::mutex> write_lock(write_mutex_);

            {
                std::lock_guard<std::mutex> state_lock(state_mutex_);
                if (state_ != ProcessState::Running || process_ != process || exited_) {
                    report_error("stdio transport is not running");
                    return false;
                }
            }

            try {
                ok = process->write(reinterpret_cast<const char*>(data), size);
            }
            catch (const std::exception& exc) {
                ok = false;
                write_error = exc.what();
            }
            catch (...) {
                ok = false;
                write_error = "unknown worker stdin write error";
            }
        }

        if (!ok) {
            const auto message = write_error.empty()
                ? std::string("failed to write bytes to worker stdin")
                : write_error;
            report_error(message);
        }

        return ok;
    }

    bool is_running() const {
        std::lock_guard<std::mutex> lock(state_mutex_);
        return state_ == ProcessState::Running && process_ != nullptr && !exited_;
    }

    void stop() {
        std::lock_guard<std::mutex> lifecycle_lock(lifecycle_mutex_);

        const bool called_from_callback_thread = is_callback_thread();

        std::shared_ptr<TinyProcessLib::Process> process;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            process = process_;
            if (state_ == ProcessState::Running || state_ == ProcessState::Starting) {
                state_ = ProcessState::Stopping;
            }
        }

        if (process != nullptr) {
            close_stdin_if_write_is_idle(process);
            wait_for_process_or_kill(process);
            join_exit_thread_if_needed();
        }
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            process_.reset();
            state_ = ProcessState::Stopped;
        }

        request_callback_thread_stop();

        if (!called_from_callback_thread) {
            join_callback_thread_if_needed();

            std::lock_guard<std::mutex> lock(state_mutex_);
            clear_handlers_locked();
        }
    }

private:
    void fail_start(const std::string& message) {
        ErrorHandler handler;
        std::shared_ptr<TinyProcessLib::Process> process;
        if (process == nullptr) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            process = process_;
            handler = error_handler_;
        }

        if (process != nullptr && is_started_process_id(process->get_id())) {
            process->kill(true);
        }

        join_exit_thread_if_needed();
        request_callback_thread_stop();
        join_callback_thread_if_needed();

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            process_.reset();
            state_ = ProcessState::Stopped;
            exited_ = false;
            clear_handlers_locked();
        }

        call_error(handler, message);
    }

    void watch_process_exit(std::shared_ptr<TinyProcessLib::Process> process) {
        // Tiny-process get_exit_status() closes pipes and joins stdout/stderr
        // reader threads before returning. Queue Exit only after that, so all
        // Receive/Stderr events produced by those readers are already queued.
        const int status = process->get_exit_status();

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            exit_status_ = status;
            exited_ = true;
            if (process_ == process && state_ != ProcessState::Stopped) {
                state_ = ProcessState::Stopped;
            }
        }
        exit_condition_.notify_all();

        CallbackEvent event;
        event.type = CallbackEventType::Exit;
        event.exit_status = status;
        enqueue_event(std::move(event));
    }

    void handle_stdout(const char* bytes, std::size_t size) {
        if (bytes == nullptr || size == 0) {
            return;
        }

        Bytes out(size);
        std::memcpy(out.data(), bytes, size);

        CallbackEvent event;
        event.type = CallbackEventType::Receive;
        event.bytes = std::move(out);
        enqueue_event(std::move(event));
    }

    void handle_stderr(const char* bytes, std::size_t size) {
        if (bytes == nullptr || size == 0 || !options_.stderr_handler) {
            return;
        }

        CallbackEvent event;
        event.type = CallbackEventType::Stderr;
        event.message.assign(bytes, size);
        enqueue_event(std::move(event));
    }

    void report_error(const std::string& message) {
        CallbackEvent event;
        event.type = CallbackEventType::Error;
        event.message = message;
        enqueue_event(std::move(event));
    }

    void start_callback_thread() {
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback_events_.clear();
            callback_stop_requested_ = false;
            callback_accepting_events_ = true;
        }

        callback_thread_ = std::thread([this]() {
            callback_loop();
        });
    }

    void callback_loop() {
        while (true) {
            CallbackEvent event;

            {
                std::unique_lock<std::mutex> lock(callback_mutex_);
                callback_condition_.wait(lock, [this]() {
                    return callback_stop_requested_ || !callback_events_.empty();
                });

                if (callback_events_.empty() && callback_stop_requested_) {
                    break;
                }

                event = std::move(callback_events_.front());
                callback_events_.pop_front();
            }

            dispatch_event(std::move(event));
        }
    }

    void dispatch_event(CallbackEvent event) {
        if (event.type == CallbackEventType::Receive) {
            ReceiveHandler handler;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                handler = receive_handler_;
            }
            if (handler) {
                handler(std::move(event.bytes));
            }
            return;
        }

        if (event.type == CallbackEventType::Error) {
            ErrorHandler handler;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                handler = error_handler_;
            }
            call_error(handler, event.message);
            return;
        }

        if (event.type == CallbackEventType::Exit) {
            ExitHandler handler;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                handler = exit_handler_;
            }
            if (handler) {
                handler(event.exit_status);
            }
            return;
        }

        if (event.type == CallbackEventType::Stderr && options_.stderr_handler) {
            options_.stderr_handler(std::move(event.message));
        }
    }

    void enqueue_event(CallbackEvent event) {
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            if (!callback_accepting_events_) {
                return;
            }
            callback_events_.push_back(std::move(event));
        }
        callback_condition_.notify_one();
    }

    void request_callback_thread_stop() {
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback_accepting_events_ = false;
            callback_stop_requested_ = true;
        }
        callback_condition_.notify_all();
    }

    void close_stdin_if_write_is_idle(const std::shared_ptr<TinyProcessLib::Process>& process) {
        std::unique_lock<std::mutex> write_lock(write_mutex_, std::try_to_lock);
        if (!write_lock.owns_lock()) {
            return;
        }

        process->close_stdin();
    }

    void wait_for_process_or_kill(const std::shared_ptr<TinyProcessLib::Process>& process) {
        bool timed_out = false;
        {
            std::unique_lock<std::mutex> lock(state_mutex_);
            timed_out = !exit_condition_.wait_for(lock, options_.shutdown_timeout, [this]() {
                return exited_;
            });
        }

        if (timed_out) {
            process->kill(true);
        }
    }

    static void call_error(const ErrorHandler& handler, const std::string& message) {
        if (handler) {
            handler(message);
        }
    }

    void join_exit_thread_if_needed() {
        if (!exit_thread_.joinable()) {
            return;
        }

        if (exit_thread_.get_id() == std::this_thread::get_id()) {
            exit_thread_.detach();
            return;
        }

        exit_thread_.join();
    }

    bool is_callback_thread() const {
        return callback_thread_.joinable()
            && callback_thread_.get_id() == std::this_thread::get_id();
    }

    void join_callback_thread_if_needed() {
        if (!callback_thread_.joinable()) {
            return;
        }

        if (is_callback_thread()) {
            return;
        }

        callback_thread_.join();
    }

    void clear_handlers_locked() {
        receive_handler_ = nullptr;
        error_handler_ = nullptr;
        exit_handler_ = nullptr;
    }

    StdIoTransportOptions options_;

    mutable std::mutex lifecycle_mutex_;
    mutable std::mutex state_mutex_;
    std::mutex write_mutex_;
    std::condition_variable exit_condition_;
    std::shared_ptr<TinyProcessLib::Process> process_;
    std::thread exit_thread_;
    std::thread callback_thread_;

    std::mutex callback_mutex_;
    std::condition_variable callback_condition_;
    std::deque<CallbackEvent> callback_events_;
    bool callback_stop_requested_ = false;
    bool callback_accepting_events_ = false;

    ReceiveHandler receive_handler_;
    ErrorHandler error_handler_;
    ExitHandler exit_handler_;

    ProcessState state_ = ProcessState::Stopped;
    bool exited_ = false;
    int exit_status_ = -1;
};

StdIoTransport::StdIoTransport(StdIoTransportOptions options)
    : impl_(std::make_unique<Impl>(std::move(options))) {}

StdIoTransport::~StdIoTransport() = default;

bool StdIoTransport::start(
    ReceiveHandler receive_handler,
    ErrorHandler error_handler,
    ExitHandler exit_handler) {
    return impl_->start(
        std::move(receive_handler),
        std::move(error_handler),
        std::move(exit_handler));
}

bool StdIoTransport::send(const std::byte* data, std::size_t size) {
    return impl_->send(data, size);
}

bool StdIoTransport::is_running() const {
    return impl_->is_running();
}

void StdIoTransport::stop() {
    impl_->stop();
}

} // namespace qwen_tts_bridge
