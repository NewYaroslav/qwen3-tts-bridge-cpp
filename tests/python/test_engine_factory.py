import math
import sys
import unittest
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT_DIR / "worker"))

from qwen_tts_bridge_worker.config import (  # noqa: E402
    MockEngineConfig,
    WorkerConfig,
)
from qwen_tts_bridge_worker.engine import (  # noqa: E402
    EngineFactoryError,
    MockTtsEngine,
    create_engine,
)
from qwen_tts_bridge_worker.engine.types import AudioFormat, SynthesisRequest  # noqa: E402


class EngineFactoryTests(unittest.TestCase):
    def test_create_mock_engine_from_config(self) -> None:
        config = WorkerConfig(
            engine="mock",
            mock=MockEngineConfig(
                chunk_count=2,
                chunk_duration_ms=40,
                chunk_delay_seconds=0.0,
            ),
        )

        engine = create_engine(config)

        self.assertIsInstance(engine, MockTtsEngine)
        self.assertFalse(engine.warmed_up)
        self.assertTrue(engine.capabilities.streaming)
        self.assertTrue(engine.capabilities.cancellation)

    def test_mock_engine_validates_supported_audio_format(self) -> None:
        engine = create_engine(WorkerConfig(engine="mock"))
        request = SynthesisRequest(
            request_id=1,
            text="test",
            output=AudioFormat.default(),
        )

        self.assertIsNone(engine.validate_request(request))

        unsupported = SynthesisRequest(
            request_id=2,
            text="test",
            output=AudioFormat(sample_rate=48000),
        )
        error = engine.validate_request(unsupported)

        self.assertIsNotNone(error)
        assert error is not None
        self.assertEqual("request_error", error.category)
        self.assertEqual("unsupported_audio_format", error.code)

    def test_qwen_engine_is_explicitly_unavailable(self) -> None:
        with self.assertRaisesRegex(EngineFactoryError, "not implemented"):
            create_engine(WorkerConfig(engine="qwen"))

    def test_reject_invalid_mock_delay(self) -> None:
        for value in (-1.0, math.inf, math.nan):
            with self.subTest(value=value):
                with self.assertRaises(ValueError):
                    create_engine(
                        WorkerConfig(
                            engine="mock",
                            mock=MockEngineConfig(chunk_delay_seconds=value),
                        )
                    )


if __name__ == "__main__":
    unittest.main()
