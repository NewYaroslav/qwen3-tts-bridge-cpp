#pragma once

/// \file ITransport.hpp
/// \brief Protocol-neutral byte transport interface.

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace qwen_tts_bridge {

/// \enum SendResult
/// \brief Result of handing outbound bytes to a transport.
enum class SendResult {
    Accepted,  ///< Bytes were accepted by the transport.
    WouldBlock, ///< Transport is alive, but cannot accept bytes immediately.
    Closed,    ///< Transport is stopped, closing, or has no running peer.
    Failed     ///< Transport write failed for a non-recoverable local reason.
};

/// \class ITransport
/// \brief Byte-oriented transport abstraction used by the protocol layer.
///
/// Implementations know how to move bytes, but do not interpret QTB frames or
/// synthesis request semantics. Receive callbacks are internal callbacks and
/// must not be confused with user-facing synthesis callbacks.
class ITransport {
public:
    /// \brief Owned byte buffer passed from a transport read callback.
    using Bytes = std::vector<std::byte>;

    /// \brief Called when arbitrary bytes are received from the worker.
    using ReceiveHandler = std::function<void(Bytes)>;

    /// \brief Called for local transport errors.
    using ErrorHandler = std::function<void(std::string)>;

    /// \brief Called when the underlying worker process or connection exits.
    using ExitHandler = std::function<void(int)>;

    virtual ~ITransport() = default;

    /// \brief Starts the transport and registers internal callbacks.
    /// \param receive_handler Receives arbitrary byte chunks from the worker.
    /// \param error_handler Receives local transport errors.
    /// \param exit_handler Receives the worker process exit status.
    /// \return True when the transport started successfully.
    ///
    /// Implementations should report expected startup failures through the
    /// return value and error callback instead of throwing them to callers.
    virtual bool start(
        ReceiveHandler receive_handler,
        ErrorHandler error_handler,
        ExitHandler exit_handler) = 0;

    /// \brief Sends bytes to the worker.
    /// \param data Pointer to bytes. Null is valid only when size is zero.
    /// \param size Number of bytes to send.
    /// \return Detailed result of accepting the outbound bytes.
    ///
    /// After a successful start, concurrent send calls are allowed and must be
    /// serialized by the transport implementation. A send attempted after
    /// shutdown starts should return quickly with `SendResult::Closed`.
    virtual SendResult send(const std::byte* data, std::size_t size) = 0;

    /// \brief Returns whether the transport currently has a running peer.
    virtual bool is_running() const = 0;

    /// \brief Stops the transport deterministically.
    ///
    /// This method is idempotent. It may block while waiting for the worker
    /// process and reader threads to exit. It may be called from a transport
    /// callback; in that case the implementation must not try to join the
    /// callback thread from itself. Destroying the transport object from one of
    /// its callbacks is not supported.
    virtual void stop() = 0;
};

} // namespace qwen_tts_bridge
