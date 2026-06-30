#pragma once

/// \file FrameParser.hpp
/// \brief Incremental parser for QwenTTSBridge protocol v1 frames.

#include <cstddef>
#include <string>
#include <vector>

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
    ///
    /// Bytes appended after a fatal framing error are ignored until `clear()`
    /// resets the parser state.
    ///
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
    ///
    /// This also clears a previous fatal framing state, allowing the parser to
    /// be reused for a new trusted byte stream.
    void clear();

private:
    void compact_buffer();
    ParseResult make_fatal(ProtocolError error, const std::string& message);

    std::vector<std::byte> buffer_;
    std::size_t read_offset_ = 0;
    bool fatal_ = false;
    ProtocolError fatal_error_ = ProtocolError::None;
    std::string fatal_message_;
};

} // namespace qwen_tts_bridge
