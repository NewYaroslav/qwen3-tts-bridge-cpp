"""TTS engine interfaces and test engines."""

from qwen_tts_bridge_worker.engine.base import TtsEngine
from qwen_tts_bridge_worker.engine.factory import EngineFactoryError, create_engine
from qwen_tts_bridge_worker.engine.mock_engine import MockTtsEngine
from qwen_tts_bridge_worker.engine.types import (
    AudioFormat,
    EngineCapabilities,
    SynthesisRequest,
    UnsupportedAudioFormatError,
)

__all__ = [
    "AudioFormat",
    "EngineCapabilities",
    "EngineFactoryError",
    "MockTtsEngine",
    "SynthesisRequest",
    "TtsEngine",
    "UnsupportedAudioFormatError",
    "create_engine",
]
