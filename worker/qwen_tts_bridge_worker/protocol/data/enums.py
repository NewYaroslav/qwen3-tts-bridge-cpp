"""Enumerations used by protocol v1 data structures."""

from __future__ import annotations

from enum import Enum, IntEnum


class FrameType(IntEnum):
    """Protocol v1 frame payload category."""

    CONTROL_JSON = 1
    AUDIO_PCM = 2
    ERROR_JSON = 3


class ParseStatus(Enum):
    """Result class returned by the streaming frame parser."""

    NEED_MORE_DATA = "need_more_data"
    FRAME_READY = "frame_ready"
    FATAL_ERROR = "fatal_error"


class ProtocolError(Enum):
    """Fatal binary framing errors detected before JSON validation."""

    NONE = "none"
    INVALID_MAGIC = "invalid_magic"
    UNSUPPORTED_PROTOCOL_VERSION = "unsupported_protocol_version"
    HEADER_TOO_SMALL = "header_too_small"
    HEADER_TOO_LARGE = "header_too_large"
    UNKNOWN_FRAME_TYPE = "unknown_frame_type"
    UNSUPPORTED_FLAGS = "unsupported_flags"
    PAYLOAD_TOO_LARGE = "payload_too_large"

