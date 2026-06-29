#pragma once

/// \file StdIoTransport.hpp
/// \brief Worker process transport over stdin/stdout pipes.

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <qwen_tts_bridge/transport/ITransport.hpp>

namespace qwen_tts_bridge {

/// \struct StdIoTransportOptions
/// \brief Process launch options for StdIoTransport.
struct StdIoTransportOptions {
    /// \brief Process arguments. The first item must be the executable path.
    std::vector<std::string> arguments;

    /// \brief Worker process working directory. Empty means inherit current.
    std::string working_directory;

    /// \brief Optional complete process environment.
    ///
    /// Leave empty to inherit the current process environment.
    std::unordered_map<std::string, std::string> environment;

    /// \brief Maximum time to wait for graceful exit before killing process.
    std::chrono::milliseconds shutdown_timeout{3000};

    /// \brief Tiny-process stdout/stderr read buffer size.
    std::size_t read_buffer_size = 64u * 1024u;

    /// \brief Optional diagnostic handler for worker stderr bytes.
    std::function<void(std::string)> stderr_handler;
};

/// \class StdIoTransport
/// \brief Starts a persistent worker process and transports bytes over pipes.
///
/// stdin carries client-to-worker protocol frames. stdout carries
/// worker-to-client protocol frames. stderr is diagnostic text and is not
/// passed to the protocol parser.
///
/// start() and stop() are serialized internally. send() may be called from
/// multiple threads after a successful start. stop() is idempotent and may be
/// called from a transport callback, but object destruction from a callback is
/// not supported.
class StdIoTransport final : public ITransport {
public:
    /// \brief Creates a stdio transport with process launch options.
    explicit StdIoTransport(StdIoTransportOptions options);

    /// \brief Stops the worker process if still running.
    ~StdIoTransport() override;

    StdIoTransport(const StdIoTransport&) = delete;
    StdIoTransport& operator=(const StdIoTransport&) = delete;

    /// \brief Starts the worker process.
    /// \return True on success; false when startup validation or process launch
    /// fails.
    bool start(
        ReceiveHandler receive_handler,
        ErrorHandler error_handler,
        ExitHandler exit_handler) override;

    /// \brief Sends bytes to worker stdin.
    /// \return True when the bytes were written; false when stopped, stopping,
    /// or the pipe write fails.
    bool send(const std::byte* data, std::size_t size) override;

    /// \brief Returns true while the worker process is considered running.
    bool is_running() const override;

    /// \brief Requests deterministic worker shutdown.
    ///
    /// This call may block until shutdown_timeout expires and the process is
    /// terminated as a fallback.
    void stop() override;

private:
    class Impl;

    std::unique_ptr<Impl> impl_;
};

} // namespace qwen_tts_bridge
