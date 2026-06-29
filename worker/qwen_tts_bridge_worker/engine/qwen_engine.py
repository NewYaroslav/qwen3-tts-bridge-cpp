"""Placeholder module for the future Qwen3-TTS engine adapter."""

from __future__ import annotations

import threading
from typing import Iterable, Optional

from qwen_tts_bridge_worker.config import QwenEngineConfig
from qwen_tts_bridge_worker.engine.types import (
    EngineCapabilities,
    EngineRequestError,
    SynthesisRequest,
)


class QwenTtsEngine:
    """Future adapter around Qwen3-TTS-streaming.

    The implementation intentionally stays unavailable until the external
    Qwen3-TTS dependency is integrated and model loading semantics are verified.
    """

    def __init__(self, config: QwenEngineConfig) -> None:
        self._config = config

    @property
    def warmed_up(self) -> bool:
        """Return whether Qwen warmup has completed."""

        return False

    @property
    def capabilities(self) -> EngineCapabilities:
        """Return the capabilities expected from the future Qwen adapter."""

        return EngineCapabilities(
            streaming=True,
            cancellation=True,
            instructions=True,
            voice_clone=False,
        )

    def load(self) -> None:
        """Load Qwen model resources."""

        raise NotImplementedError("qwen engine integration is not implemented yet")

    def warmup(self) -> None:
        """Run Qwen warmup."""

        raise NotImplementedError("qwen engine integration is not implemented yet")

    def validate_request(
        self,
        request: SynthesisRequest,
    ) -> Optional[EngineRequestError]:
        """Validate a Qwen synthesis request."""

        del request
        return None

    def synthesize_stream(
        self,
        request: SynthesisRequest,
        cancel_event: threading.Event,
    ) -> Iterable[bytes]:
        """Yield PCM chunks from Qwen synthesis."""

        del request, cancel_event
        raise NotImplementedError("qwen engine integration is not implemented yet")

    def close(self) -> None:
        """Release Qwen resources."""
