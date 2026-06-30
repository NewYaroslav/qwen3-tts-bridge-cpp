"""Runtime configuration DTOs for the Python worker."""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Literal, TypeAlias


@dataclass(frozen=True, slots=True)
class MockEngineConfig:
    """Configuration for the deterministic mock engine."""

    chunk_count: int = 3
    chunk_duration_ms: int = 100
    chunk_delay_seconds: float = 0.0
    kind: Literal["mock"] = field(default="mock", init=False)

    def __post_init__(self) -> None:
        """Validate mock-engine settings when the DTO is created."""

        if self.chunk_count <= 0:
            raise ValueError("mock.chunk_count must be greater than zero")
        if self.chunk_duration_ms < 20:
            raise ValueError("mock.chunk_duration_ms must be at least 20")
        if (
            not math.isfinite(self.chunk_delay_seconds)
            or self.chunk_delay_seconds < 0.0
        ):
            raise ValueError("mock.chunk_delay_seconds must be finite and non-negative")


@dataclass(frozen=True, slots=True)
class QwenEngineConfig:
    """Configuration placeholder for the future Qwen3-TTS engine adapter."""

    model_path: str = ""
    device: str = "cuda"
    dtype: str = "auto"
    kind: Literal["qwen"] = field(default="qwen", init=False)

    def __post_init__(self) -> None:
        """Validate settings shared by the future Qwen engine adapter."""

        if not self.device:
            raise ValueError("qwen.device must not be empty")
        if not self.dtype:
            raise ValueError("qwen.dtype must not be empty")


EngineConfig: TypeAlias = MockEngineConfig | QwenEngineConfig


@dataclass(frozen=True, slots=True)
class WorkerConfig:
    """Top-level worker runtime configuration."""

    worker_version: str = "0.2.0"
    output_queue_size: int = 128
    engine: EngineConfig = field(default_factory=MockEngineConfig)

    def __post_init__(self) -> None:
        """Validate worker-level settings when the DTO is created."""

        if not self.worker_version:
            raise ValueError("worker_version must not be empty")
        if self.output_queue_size <= 0:
            raise ValueError("output_queue_size must be greater than zero")
        if not isinstance(self.engine, (MockEngineConfig, QwenEngineConfig)):
            raise TypeError("engine must be a known engine configuration")
