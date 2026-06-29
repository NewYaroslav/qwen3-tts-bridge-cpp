#pragma once

/// \file FrameParser.hpp
/// \brief Incremental parser for QwenTTSBridge protocol v1 frames.

#include <cstddef>
#include <vector>

#include <qwen_tts_bridge/data.hpp>

namespace qwen_tts_bridge {

/// \class FrameParser
/// \brief Incremental parser for the protocol v1 byte stream.
///
/// The parser accepts arbitrary byte chunks from stdin/stdout or another
/// byte-oriented transport. It returns complete frames when enough bytes have
/// accumulated and reports fatal framing errors without attempting stream
/// resynchronization.
class FrameParser {
public:
    /// \brief Appends bytes to the internal parser buffer.
    /// \param data Pointer to bytes. Null pointers are ignored.
    /// \param size Number of bytes to append.
    void append(const std::byte* data, std::size_t size);

    /// \brief Appends a byte vector to the internal parser buffer.
    /// \param data Bytes to append.
    void append(const std::vector<std::byte>& data);

    /// \brief Parses the next available frame.
    /// \return Parse result. NeedMoreData means the buffer remains usable.
    ParseResult parse_next();

    /// \brief Returns currently buffered byte count.
    std::size_t buffered_size() const;

    /// \brief Clears all buffered bytes.
    void clear();

private:
    std::vector<std::byte> buffer_;
};

} // namespace qwen_tts_bridge
