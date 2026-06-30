"""Engine interface used by worker servers."""

from __future__ import annotations

import threading
from collections.abc import Iterable
from typing import Protocol

from qwen_tts_bridge_worker.engine.types import (
    EngineCapabilities,
    SynthesisRequest,
)


class TtsEngine(Protocol):
    """Narrow synthesis engine interface hidden behind the worker server."""

    @property
    def capabilities(self) -> EngineCapabilities:
        """Return capabilities exposed by this engine."""
        ...

    def load(self) -> None:
        """Load model resources needed before the worker can become ready."""
        ...

    def warmup(self) -> None:
        """Run optional warmup before the worker sends ready."""
        ...

    def validate_request(
        self,
        request: SynthesisRequest,
    ) -> None:
        """Raise an engine-domain error if the request cannot be satisfied."""
        ...

    def synthesize_stream(
        self,
        request: SynthesisRequest,
        cancel_event: threading.Event,
    ) -> Iterable[bytes]:
        """Yield PCM chunks for one synthesis request."""
        ...

    def close(self) -> None:
        """Release engine resources during worker shutdown."""
        ...
