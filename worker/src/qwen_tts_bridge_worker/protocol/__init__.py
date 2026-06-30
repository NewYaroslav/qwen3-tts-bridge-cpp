"""Protocol v1 package for the Python worker.

This module is an umbrella import surface. Keep implementation code in
subdomains such as :mod:`protocol.data` and :mod:`protocol.framing`.
"""

from qwen_tts_bridge_worker.protocol.data import (
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
    FrameType,
    ParseResult,
    ParseStatus,
    ProtocolError,
)
from qwen_tts_bridge_worker.protocol.framing import (
    FrameParser,
    encode_frame,
    encode_frame_with_header,
    is_known_frame_type,
    max_payload_size,
)

__all__ = [
    "BytesLike",
    "Frame",
    "FrameHeader",
    "FrameParser",
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
    "encode_frame",
    "encode_frame_with_header",
    "is_known_frame_type",
    "max_payload_size",
]

