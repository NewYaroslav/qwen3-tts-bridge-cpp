"""Factory for worker engine implementations."""

from __future__ import annotations

from qwen_tts_bridge_worker.config import WorkerConfig
from qwen_tts_bridge_worker.engine.base import TtsEngine
from qwen_tts_bridge_worker.engine.mock_engine import MockTtsEngine


class EngineFactoryError(RuntimeError):
    """Raised when the requested engine cannot be created."""


def create_engine(config: WorkerConfig) -> TtsEngine:
    """Create an engine instance from worker configuration."""

    config.validate()

    if config.engine == "mock":
        return MockTtsEngine(
            chunk_count=config.mock.chunk_count,
            chunk_duration_ms=config.mock.chunk_duration_ms,
            chunk_delay_seconds=config.mock.chunk_delay_seconds,
        )

    if config.engine == "qwen":
        raise EngineFactoryError("qwen engine integration is not implemented yet")

    raise EngineFactoryError(f"unsupported engine: {config.engine}")
