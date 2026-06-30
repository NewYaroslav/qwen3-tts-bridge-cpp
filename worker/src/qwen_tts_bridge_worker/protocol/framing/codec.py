"""Binary frame encoding and frame-type rules for protocol v1."""

from __future__ import annotations

import struct

from qwen_tts_bridge_worker.protocol.data import (
    MAGIC,
    MAX_AUDIO_PAYLOAD_BYTES,
    MAX_CONTROL_PAYLOAD_BYTES,
    MAX_ERROR_PAYLOAD_BYTES,
    MAX_FRAME_PAYLOAD_BYTES,
    MIN_HEADER_SIZE,
    PROTOCOL_VERSION,
    BytesLike,
    FrameHeader,
    FrameType,
)

HEADER_STRUCT = struct.Struct("<4sHHHHIQ")


def is_known_frame_type(value: int) -> bool:
    """Return True when value maps to a known FrameType."""

    try:
        FrameType(value)
    except ValueError:
        return False
    return True


def max_payload_size(frame_type: FrameType) -> int:
    """Return the maximum allowed payload size for a frame type."""

    if frame_type == FrameType.CONTROL_JSON:
        return MAX_CONTROL_PAYLOAD_BYTES
    if frame_type == FrameType.AUDIO_PCM:
        return MAX_AUDIO_PAYLOAD_BYTES
    if frame_type == FrameType.ERROR_JSON:
        return MAX_ERROR_PAYLOAD_BYTES
    return 0


def encode_frame(
    frame_type: FrameType,
    request_id: int,
    payload: BytesLike,
) -> bytes:
    """Encode a protocol frame with a default v1 header."""

    payload_bytes = bytes(payload)
    header = FrameHeader(
        frame_type=FrameType(frame_type),
        payload_size=len(payload_bytes),
        request_id=request_id,
    )
    return encode_frame_with_header(header, payload_bytes)


def encode_frame_with_header(header: FrameHeader, payload: BytesLike) -> bytes:
    """Encode a protocol frame with an explicit v1 header."""

    payload_bytes = bytes(payload)
    _validate_outgoing_frame(header, len(payload_bytes))
    return HEADER_STRUCT.pack(
        MAGIC,
        header.protocol_version,
        header.header_size,
        int(header.frame_type),
        header.flags,
        len(payload_bytes),
        header.request_id,
    ) + payload_bytes


def _validate_outgoing_frame(header: FrameHeader, payload_size: int) -> None:
    if header.protocol_version != PROTOCOL_VERSION:
        raise ValueError("unsupported protocol version")
    if header.header_size != MIN_HEADER_SIZE:
        raise ValueError("v1 senders must use the fixed 24-byte header")
    if not is_known_frame_type(int(header.frame_type)):
        raise ValueError("unknown frame type")
    if header.flags != 0:
        raise ValueError("unsupported frame flags")
    if not 0 <= header.request_id <= 0xFFFFFFFFFFFFFFFF:
        raise ValueError("request_id is outside u64 range")
    if header.payload_size != payload_size:
        raise ValueError("header payload_size does not match payload bytes")
    if payload_size > MAX_FRAME_PAYLOAD_BYTES:
        raise ValueError("payload size exceeds absolute frame limit")
    if payload_size > max_payload_size(FrameType(header.frame_type)):
        raise ValueError("payload size exceeds frame type limit")
