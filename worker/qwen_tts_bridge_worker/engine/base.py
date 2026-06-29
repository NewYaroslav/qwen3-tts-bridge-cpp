"""Engine interface used by worker servers."""

from __future__ import annotations

import threading
from typing import Iterable, Protocol

from qwen_tts_bridge_worker.engine.types import SynthesisRequest


class TtsEngine(Protocol):
    """Narrow synthesis engine interface hidden behind the worker server."""

    def load(self) -> None:
        """Load model resources needed before the worker can become ready."""

    def warmup(self) -> None:
        """Run optional warmup before the worker sends ready."""

    def synthesize_stream(
        self,
        request: SynthesisRequest,
        cancel_event: threading.Event,
    ) -> Iterable[bytes]:
        """Yield PCM chunks for one synthesis request."""

    def close(self) -> None:
        """Release engine resources during worker shutdown."""

