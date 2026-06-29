"""Deterministic PCM-producing mock engine for protocol and transport tests."""

from __future__ import annotations

import math
import struct
import threading
import time
from typing import Iterable, Optional

from qwen_tts_bridge_worker.engine.types import (
    AudioFormat,
    EngineCapabilities,
    EngineRequestError,
    SynthesisRequest,
)


class MockTtsEngine:
    """Small deterministic engine that emits s16le sine-wave PCM chunks."""

    def __init__(
        self,
        chunk_count: int = 3,
        chunk_duration_ms: int = 100,
        chunk_delay_seconds: float = 0.0,
    ) -> None:
        self._chunk_count = max(1, chunk_count)
        self._chunk_duration_ms = max(20, chunk_duration_ms)
        self._chunk_delay_seconds = max(0.0, chunk_delay_seconds)
        self._loaded = False
        self._warmed_up = False

    @property
    def warmed_up(self) -> bool:
        """Return whether warmup has completed."""

        return self._warmed_up

    @property
    def capabilities(self) -> EngineCapabilities:
        """Return protocol capabilities supported by the mock engine."""

        return EngineCapabilities(
            streaming=True,
            cancellation=True,
            instructions=True,
            voice_clone=False,
        )

    def load(self) -> None:
        """Mark the mock model as loaded."""

        self._loaded = True

    def warmup(self) -> None:
        """Mark warmup as completed."""

        if not self._loaded:
            self.load()
        self._warmed_up = True

    def validate_request(
        self,
        request: SynthesisRequest,
    ) -> Optional[EngineRequestError]:
        """Validate that the mock engine can satisfy the requested output."""

        if request.output == AudioFormat.default():
            return None
        return EngineRequestError(
            category="request_error",
            code="unsupported_audio_format",
            message="mock engine supports only s16le 24000 Hz mono",
        )

    def synthesize_stream(
        self,
        request: SynthesisRequest,
        cancel_event: threading.Event,
    ) -> Iterable[bytes]:
        """Yield deterministic PCM chunks until completed or cancelled."""

        del request
        samples_per_chunk = (
            AudioFormat.default().sample_rate * self._chunk_duration_ms // 1000
        )

        sample_index = 0
        for _chunk_index in range(self._chunk_count):
            if cancel_event.is_set():
                return
            yield _sine_chunk(samples_per_chunk, sample_index)
            sample_index += samples_per_chunk
            if self._chunk_delay_seconds > 0:
                time.sleep(self._chunk_delay_seconds)

    def close(self) -> None:
        """Release mock resources."""

        self._loaded = False


def _sine_chunk(samples: int, start_sample: int) -> bytes:
    out = bytearray()
    sample_rate = AudioFormat.default().sample_rate
    frequency_hz = 220.0
    amplitude = 3200

    for index in range(samples):
        t = (start_sample + index) / sample_rate
        sample = int(math.sin(2.0 * math.pi * frequency_hz * t) * amplitude)
        out.extend(struct.pack("<h", sample))

    return bytes(out)
