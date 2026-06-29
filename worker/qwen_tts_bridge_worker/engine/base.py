"""Engine interface used by worker servers."""

from __future__ import annotations

import threading
from typing import Iterable, Optional, Protocol

from qwen_tts_bridge_worker.engine.types import (
    EngineCapabilities,
    EngineRequestError,
    SynthesisRequest,
)


class TtsEngine(Protocol):
    """Narrow synthesis engine interface hidden behind the worker server."""

    @property
    def warmed_up(self) -> bool:
        """Return whether warmup was completed successfully."""

    @property
    def capabilities(self) -> EngineCapabilities:
        """Return protocol capabilities exposed by this engine."""

    def load(self) -> None:
        """Load model resources needed before the worker can become ready."""

    def warmup(self) -> None:
        """Run optional warmup before the worker sends ready."""

    def validate_request(
        self,
        request: SynthesisRequest,
    ) -> Optional[EngineRequestError]:
        """Return a request error if the engine cannot satisfy the request."""

    def synthesize_stream(
        self,
        request: SynthesisRequest,
        cancel_event: threading.Event,
    ) -> Iterable[bytes]:
        """Yield PCM chunks for one synthesis request."""

    def close(self) -> None:
        """Release engine resources during worker shutdown."""
