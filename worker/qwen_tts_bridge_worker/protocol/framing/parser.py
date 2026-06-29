"""Incremental binary frame parser for protocol v1."""

from __future__ import annotations

from qwen_tts_bridge_worker.protocol.data import (
    MAGIC,
    MAX_FRAME_PAYLOAD_BYTES,
    MAX_HEADER_SIZE,
    MIN_HEADER_SIZE,
    PROTOCOL_VERSION,
    BytesLike,
    Frame,
    FrameHeader,
    FrameType,
    ParseResult,
    ParseStatus,
    ProtocolError,
)
from qwen_tts_bridge_worker.protocol.framing.codec import (
    HEADER_STRUCT,
    max_payload_size,
)


class FrameParser:
    """Incremental parser for protocol v1 byte streams."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def append(self, data: BytesLike) -> None:
        """Append bytes from an arbitrary transport read."""

        if not data:
            return
        self._buffer.extend(data)

    def parse_next(self) -> ParseResult:
        """Parse and return the next complete frame when available."""

        if len(self._buffer) < MIN_HEADER_SIZE:
            return ParseResult()

        if bytes(self._buffer[:4]) != MAGIC:
            return _fatal(ProtocolError.INVALID_MAGIC, "invalid frame magic")

        (
            _magic,
            protocol_version,
            header_size,
            frame_type_value,
            flags,
            payload_size,
            request_id,
        ) = HEADER_STRUCT.unpack_from(self._buffer)

        if protocol_version != PROTOCOL_VERSION:
            return _fatal(
                ProtocolError.UNSUPPORTED_PROTOCOL_VERSION,
                "unsupported protocol version",
            )

        if header_size < MIN_HEADER_SIZE:
            return _fatal(
                ProtocolError.HEADER_TOO_SMALL,
                "header size is smaller than v1 minimum",
            )

        if header_size > MAX_HEADER_SIZE:
            return _fatal(
                ProtocolError.HEADER_TOO_LARGE,
                "header size exceeds v1 maximum",
            )

        try:
            frame_type = FrameType(frame_type_value)
        except ValueError:
            return _fatal(ProtocolError.UNKNOWN_FRAME_TYPE, "unknown frame type")

        if flags != 0:
            return _fatal(ProtocolError.UNSUPPORTED_FLAGS, "unsupported frame flags")

        if (
            payload_size > MAX_FRAME_PAYLOAD_BYTES
            or payload_size > max_payload_size(frame_type)
        ):
            return _fatal(
                ProtocolError.PAYLOAD_TOO_LARGE,
                "payload size exceeds v1 limit",
            )

        total_size = header_size + payload_size
        if len(self._buffer) < total_size:
            return ParseResult()

        payload = bytes(self._buffer[header_size:total_size])
        del self._buffer[:total_size]

        header = FrameHeader(
            protocol_version=protocol_version,
            header_size=header_size,
            frame_type=frame_type,
            flags=flags,
            payload_size=payload_size,
            request_id=request_id,
        )
        return ParseResult(
            status=ParseStatus.FRAME_READY,
            frame=Frame(header=header, payload=payload),
        )

    @property
    def buffered_size(self) -> int:
        """Return currently buffered byte count."""

        return len(self._buffer)

    def clear(self) -> None:
        """Clear all buffered bytes."""

        self._buffer.clear()


def _fatal(error: ProtocolError, message: str) -> ParseResult:
    return ParseResult(
        status=ParseStatus.FATAL_ERROR,
        error=error,
        message=message,
    )
