"""Shared protocol v1 data types and constants."""

from qwen_tts_bridge_worker.protocol.data.enums import (
    FrameType,
    ParseStatus,
    ProtocolError,
)
from qwen_tts_bridge_worker.protocol.data.protocol_types import (
    MAGIC,
    MAX_AUDIO_PAYLOAD_BYTES,
    MAX_CONTROL_PAYLOAD_BYTES,
    MAX_ERROR_PAYLOAD_BYTES,
    MAX_FRAME_PAYLOAD_BYTES,
    MAX_HEADER_SIZE,
    MIN_HEADER_SIZE,
    PROTOCOL_VERSION,
    BytesLike,
    Frame,
    FrameHeader,
    ParseResult,
)

__all__ = [
    "BytesLike",
    "Frame",
    "FrameHeader",
    "FrameType",
    "MAGIC",
    "MAX_AUDIO_PAYLOAD_BYTES",
    "MAX_CONTROL_PAYLOAD_BYTES",
    "MAX_ERROR_PAYLOAD_BYTES",
    "MAX_FRAME_PAYLOAD_BYTES",
    "MAX_HEADER_SIZE",
    "MIN_HEADER_SIZE",
    "PROTOCOL_VERSION",
    "ParseResult",
    "ParseStatus",
    "ProtocolError",
]

