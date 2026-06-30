"""Runtime configuration DTOs for the Python worker."""

from __future__ import annotations

import math
from dataclasses import dataclass, field


@dataclass(frozen=True)
class MockEngineConfig:
    """Configuration for the deterministic mock engine."""

    chunk_count: int = 3
    chunk_duration_ms: int = 100
    chunk_delay_seconds: float = 0.0

    def validate(self) -> None:
        """Validate mock-engine settings."""

        if self.chunk_count <= 0:
            raise ValueError("mock.chunk_count must be greater than zero")
        if self.chunk_duration_ms <= 0:
            raise ValueError("mock.chunk_duration_ms must be greater than zero")
        if not math.isfinite(self.chunk_delay_seconds) or self.chunk_delay_seconds < 0.0:
            raise ValueError("mock.chunk_delay_seconds must be finite and non-negative")


@dataclass(frozen=True)
class QwenEngineConfig:
    """Configuration placeholder for the future Qwen3-TTS engine adapter."""

    model_path: str = ""
    device: str = "cuda"
    dtype: str = "auto"

    def validate(self) -> None:
        """Validate settings shared by the future Qwen engine adapter."""

        if not self.device:
            raise ValueError("qwen.device must not be empty")
        if not self.dtype:
            raise ValueError("qwen.dtype must not be empty")


@dataclass(frozen=True)
class WorkerConfig:
    """Top-level worker runtime configuration."""

    worker_version: str = "0.2.0"
    output_queue_size: int = 128
    engine: str = "mock"
    mock: MockEngineConfig = field(default_factory=MockEngineConfig)
    qwen: QwenEngineConfig = field(default_factory=QwenEngineConfig)

    def validate(self) -> None:
        """Validate configuration values."""

        if not self.worker_version:
            raise ValueError("worker_version must not be empty")
        if self.output_queue_size <= 0:
            raise ValueError("output_queue_size must be greater than zero")
        if self.engine == "mock":
            self.mock.validate()
        elif self.engine == "qwen":
            self.qwen.validate()
        else:
            raise ValueError(f"unsupported engine: {self.engine}")
