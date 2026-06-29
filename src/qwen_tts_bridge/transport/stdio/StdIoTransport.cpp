#include <qwen_tts_bridge/transport/stdio/StdIoTransport.hpp>

#include <process.hpp>

#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace qwen_tts_bridge {
namespace {

TinyProcessLib::Process::string_type to_process_string(const std::string& value) {
#if defined(_WIN32) && defined(UNICODE)
    return TinyProcessLib::Process::string_type(value.begin(), value.end());
#else
    return value;
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
        if (options_.arguments.empty()) {
            call_error(error_handler, "stdio transport requires at least one process argument");
            return false;
        }

        bool already_started = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            already_started = process_ != nullptr;
            if (!already_started) {
                receive_handler_ = std::move(receive_handler);
                error_handler_ = std::move(error_handler);
                exit_handler_ = std::move(exit_handler);
                running_ = false;
                stopping_ = false;
                exited_ = false;
                exit_status_ = -1;
            }
        }

        if (already_started) {
            call_error(error_handler, "stdio transport is already started");
            return false;
        }

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

        std::unique_ptr<TinyProcessLib::Process> process;
        if (options_.environment.empty()) {
            process = std::make_unique<TinyProcessLib::Process>(
                arguments,
                working_directory,
                stdout_handler,
                stderr_handler,
                true,
                config);
        }
        else {
            auto environment = to_process_environment(options_.environment);
            process = std::make_unique<TinyProcessLib::Process>(
                arguments,
                working_directory,
                environment,
                stdout_handler,
                stderr_handler,
                true,
                config);
        }

        if (process->get_id() == 0) {
            ErrorHandler handler;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                handler = error_handler_;
                clear_handlers_locked();
            }
            call_error(handler, "failed to start stdio worker process");
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            process_ = std::move(process);
            running_ = true;
        }

        exit_thread_ = std::thread([this]() {
            watch_process_exit();
        });

        return true;
    }

    bool send(const std::byte* data, std::size_t size) {
        if (data == nullptr && size != 0) {
            report_error("cannot send a non-zero byte count from a null pointer");
            return false;
        }

        if (size == 0) {
            return true;
        }

        bool ok = false;
        std::string write_error;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (process_ == nullptr || !running_) {
                ok = false;
            }
            else {
                try {
                    ok = process_->write(reinterpret_cast<const char*>(data), size);
                }
                catch (const std::exception& exc) {
                    ok = false;
                    write_error = exc.what();
                }
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
        std::lock_guard<std::mutex> lock(mutex_);
        return running_;
    }

    void stop() {
        TinyProcessLib::Process* process = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (process_ == nullptr) {
                process = nullptr;
            }
            else {
                stopping_ = true;
                process = process_.get();
            }
        }

        if (process == nullptr) {
            join_exit_thread_if_needed();
            return;
        }

        process->close_stdin();

        bool timed_out = false;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            timed_out = !exit_condition_.wait_for(lock, options_.shutdown_timeout, [this]() {
                return exited_;
            });
        }

        if (timed_out) {
            process->kill(true);
        }

        join_exit_thread_if_needed();

        {
            std::lock_guard<std::mutex> lock(mutex_);
            process_.reset();
            running_ = false;
            stopping_ = false;
            clear_handlers_locked();
        }
    }

private:
    void watch_process_exit() {
        TinyProcessLib::Process* process = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            process = process_.get();
        }

        if (process == nullptr) {
            return;
        }

        const int status = process->get_exit_status();

        ExitHandler handler;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            exit_status_ = status;
            exited_ = true;
            running_ = false;
            handler = exit_handler_;
        }
        exit_condition_.notify_all();

        if (handler) {
            handler(status);
        }
    }

    void handle_stdout(const char* bytes, std::size_t size) {
        if (bytes == nullptr || size == 0) {
            return;
        }

        Bytes out(size);
        std::memcpy(out.data(), bytes, size);

        ReceiveHandler handler;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            handler = receive_handler_;
        }

        if (handler) {
            handler(std::move(out));
        }
    }

    void handle_stderr(const char* bytes, std::size_t size) {
        if (bytes == nullptr || size == 0 || !options_.stderr_handler) {
            return;
        }

        options_.stderr_handler(std::string(bytes, size));
    }

    void report_error(const std::string& message) {
        ErrorHandler handler;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            handler = error_handler_;
        }
        call_error(handler, message);
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

    void clear_handlers_locked() {
        receive_handler_ = nullptr;
        error_handler_ = nullptr;
        exit_handler_ = nullptr;
    }

    StdIoTransportOptions options_;

    mutable std::mutex mutex_;
    std::condition_variable exit_condition_;
    std::unique_ptr<TinyProcessLib::Process> process_;
    std::thread exit_thread_;

    ReceiveHandler receive_handler_;
    ErrorHandler error_handler_;
    ExitHandler exit_handler_;

    bool running_ = false;
    bool stopping_ = false;
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
