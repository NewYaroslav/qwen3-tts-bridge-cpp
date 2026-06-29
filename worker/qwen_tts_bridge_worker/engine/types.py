"""Worker engine DTOs."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Mapping


@dataclass(frozen=True)
class EngineCapabilities:
    """Feature flags announced by an engine in the ready message."""

    streaming: bool = True
    cancellation: bool = True
    instructions: bool = True
    voice_clone: bool = False

    def to_payload(self) -> dict[str, bool]:
        """Convert capabilities to protocol JSON fields."""

        return {
            "streaming": self.streaming,
            "cancellation": self.cancellation,
            "instructions": self.instructions,
            "voice_clone": self.voice_clone,
        }


@dataclass(frozen=True)
class EngineRequestError:
    """Structured request validation error produced by an engine."""

    category: str
    code: str
    message: str


@dataclass(frozen=True)
class AudioFormat:
    """PCM audio format used for worker output."""

    sample_format: str = "s16le"
    sample_rate: int = 24000
    channels: int = 1

    @staticmethod
    def default() -> "AudioFormat":
        """Return the protocol v1 default audio format."""

        return AudioFormat()

    def to_payload(self) -> dict[str, Any]:
        """Convert the format to protocol JSON fields."""

        return {
            "sample_format": self.sample_format,
            "sample_rate": self.sample_rate,
            "channels": self.channels,
        }

    @staticmethod
    def from_payload(payload: Mapping[str, Any] | None) -> "AudioFormat":
        """Parse an optional protocol output-format object."""

        if payload is None:
            return AudioFormat.default()
        return AudioFormat(
            sample_format=str(payload.get("sample_format", "s16le")),
            sample_rate=int(payload.get("sample_rate", 24000)),
            channels=int(payload.get("channels", 1)),
        )


@dataclass(frozen=True)
class SynthesisRequest:
    """Normalized synthesis request passed from server to engine."""

    request_id: int
    text: str
    language: str = "auto"
    speaker: str = "default"
    instruction: str = ""
    output: AudioFormat = field(default_factory=AudioFormat.default)
