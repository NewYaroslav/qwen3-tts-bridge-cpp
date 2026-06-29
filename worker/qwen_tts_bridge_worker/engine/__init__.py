"""TTS engine interfaces and test engines."""

from qwen_tts_bridge_worker.engine.base import TtsEngine
from qwen_tts_bridge_worker.engine.factory import EngineFactoryError, create_engine
from qwen_tts_bridge_worker.engine.mock_engine import MockTtsEngine
from qwen_tts_bridge_worker.engine.types import (
    AudioFormat,
    EngineCapabilities,
    EngineRequestError,
    SynthesisRequest,
)

__all__ = [
    "AudioFormat",
    "EngineCapabilities",
    "EngineFactoryError",
    "EngineRequestError",
    "MockTtsEngine",
    "SynthesisRequest",
    "TtsEngine",
    "create_engine",
]
