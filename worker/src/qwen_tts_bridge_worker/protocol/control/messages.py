"""JSON control-message encoding and validation for protocol v1."""

from __future__ import annotations

import json
from typing import Any, Mapping

from qwen_tts_bridge_worker.protocol.data import FrameType
from qwen_tts_bridge_worker.protocol.framing import encode_frame


class ControlMessageError(Exception):
    """Recoverable protocol error found in a control JSON payload."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(message)
        self.code = code
        self.message = message


def decode_control_payload(payload: bytes) -> dict[str, Any]:
    """Decode and validate a control JSON object payload."""

    try:
        value = json.loads(payload.decode("utf-8"))
    except UnicodeDecodeError as exc:
        raise ControlMessageError(
            "invalid_json",
            "control payload is not UTF-8",
        ) from exc
    except json.JSONDecodeError as exc:
        raise ControlMessageError(
            "invalid_json",
            "control payload is not valid JSON",
        ) from exc

    if not isinstance(value, dict):
        raise ControlMessageError(
            "payload_not_object",
            "control payload must be an object",
        )

    if "protocol_version" in value or "request_id" in value:
        raise ControlMessageError(
            "invalid_field_type",
            "control payload must not duplicate frame header fields",
        )

    message_type = value.get("message_type")
    if message_type is None:
        raise ControlMessageError("missing_message_type", "missing message_type")
    if not isinstance(message_type, str):
        raise ControlMessageError(
            "invalid_message_type",
            "message_type must be a string",
        )

    return value


def encode_json_payload(message: Mapping[str, Any]) -> bytes:
    """Encode a JSON object as compact UTF-8 bytes."""

    return json.dumps(
        dict(message),
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode("utf-8")


def control_frame(request_id: int, message: Mapping[str, Any]) -> bytes:
    """Encode a worker-to-client control frame."""

    return encode_frame(
        FrameType.CONTROL_JSON,
        request_id,
        encode_json_payload(message),
    )


def error_frame(
    request_id: int,
    category: str,
    code: str,
    message: str,
) -> bytes:
    """Encode a worker-to-client error_json frame."""

    return encode_frame(
        FrameType.ERROR_JSON,
        request_id,
        encode_json_payload(
            {
                "message_type": "error",
                "category": category,
                "code": code,
                "message": message,
            }
        ),
    )
