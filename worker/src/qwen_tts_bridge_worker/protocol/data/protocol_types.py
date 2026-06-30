"""DTOs and constants for QwenTTSBridge protocol v1 framing."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Optional, Union

from qwen_tts_bridge_worker.protocol.data.enums import (
    FrameType,
    ParseStatus,
    ProtocolError,
)

BytesLike = Union[bytes, bytearray, memoryview]


PROTOCOL_VERSION = 1
MAGIC = b"QTB1"
MIN_HEADER_SIZE = 24
MAX_HEADER_SIZE = 256

MAX_CONTROL_PAYLOAD_BYTES = 1024 * 1024
MAX_AUDIO_PAYLOAD_BYTES = 16 * 1024 * 1024
MAX_ERROR_PAYLOAD_BYTES = 1024 * 1024
MAX_FRAME_PAYLOAD_BYTES = 16 * 1024 * 1024


@dataclass(frozen=True)
class FrameHeader:
    """Parsed binary header that precedes every protocol frame."""

    protocol_version: int = PROTOCOL_VERSION
    header_size: int = MIN_HEADER_SIZE
    frame_type: FrameType = FrameType.CONTROL_JSON
    flags: int = 0
    payload_size: int = 0
    request_id: int = 0


@dataclass(frozen=True)
class Frame:
    """Complete parsed protocol frame."""

    header: FrameHeader
    payload: bytes


@dataclass(frozen=True)
class ParseResult:
    """Result returned by FrameParser.parse_next()."""

    status: ParseStatus = ParseStatus.NEED_MORE_DATA
    error: ProtocolError = ProtocolError.NONE
    message: str = ""
    frame: Optional[Frame] = None

