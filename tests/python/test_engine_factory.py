import math
import sys
import unittest
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT_DIR / "worker"))

from qwen_tts_bridge_worker.config import (  # noqa: E402
    MockEngineConfig,
    QwenEngineConfig,
    WorkerConfig,
)
from qwen_tts_bridge_worker.engine import (  # noqa: E402
    EngineFactoryError,
    MockTtsEngine,
    UnsupportedAudioFormatError,
    create_engine,
)
from qwen_tts_bridge_worker.engine.types import AudioFormat, SynthesisRequest  # noqa: E402


class EngineFactoryTests(unittest.TestCase):
    def test_create_mock_engine_from_config(self) -> None:
        config = MockEngineConfig(
            chunk_count=2,
            chunk_duration_ms=40,
            chunk_delay_seconds=0.0,
        )

        engine = create_engine(config)

        self.assertIsInstance(engine, MockTtsEngine)
        self.assertTrue(engine.capabilities.streaming)
        self.assertTrue(engine.capabilities.cancellation)

    def test_mock_engine_validates_supported_audio_format(self) -> None:
        engine = create_engine(MockEngineConfig())
        request = SynthesisRequest(
            request_id=1,
            text="test",
            output=AudioFormat.default(),
        )

        engine.validate_request(request)

        unsupported = SynthesisRequest(
            request_id=2,
            text="test",
            output=AudioFormat(sample_rate=48000),
        )

        with self.assertRaisesRegex(UnsupportedAudioFormatError, "s16le"):
            engine.validate_request(unsupported)

    def test_qwen_engine_is_explicitly_unavailable(self) -> None:
        with self.assertRaisesRegex(EngineFactoryError, "not implemented"):
            create_engine(QwenEngineConfig())

    def test_worker_config_stores_only_selected_engine_config(self) -> None:
        config = WorkerConfig(engine=QwenEngineConfig())

        self.assertIsInstance(config.engine, QwenEngineConfig)

    def test_qwen_config_rejects_empty_device_and_dtype(self) -> None:
        for qwen_config in (
            {"device": ""},
            {"dtype": ""},
        ):
            with self.subTest(qwen_config=qwen_config):
                with self.assertRaises(ValueError):
                    QwenEngineConfig(**qwen_config)

    def test_reject_invalid_mock_delay(self) -> None:
        for value in (-1.0, math.inf, math.nan):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    MockEngineConfig(chunk_delay_seconds=value)


if __name__ == "__main__":
    unittest.main()
