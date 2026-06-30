"""Factory for worker engine implementations."""

from __future__ import annotations

from qwen_tts_bridge_worker.config import (
    EngineConfig,
    MockEngineConfig,
    QwenEngineConfig,
)
from qwen_tts_bridge_worker.engine.base import TtsEngine
from qwen_tts_bridge_worker.engine.mock_engine import MockTtsEngine
from qwen_tts_bridge_worker.engine.qwen_engine import QwenTtsEngine


class EngineFactoryError(RuntimeError):
    """Raised when the requested engine cannot be created."""


def create_engine(config: EngineConfig) -> TtsEngine:
    """Create an engine instance from engine-specific configuration."""

    if isinstance(config, MockEngineConfig):
        return MockTtsEngine(
            chunk_count=config.chunk_count,
            chunk_duration_ms=config.chunk_duration_ms,
            chunk_delay_seconds=config.chunk_delay_seconds,
        )

    if isinstance(config, QwenEngineConfig):
        return QwenTtsEngine(config)

    raise EngineFactoryError(f"unsupported engine config: {type(config).__name__}")
