import math
import unittest

from qwen_tts_bridge_worker.cli import (
    build_engine_config,
    build_parser,
    build_worker_config,
)
from qwen_tts_bridge_worker.config import (
    MockEngineConfig,
    QwenEngineConfig,
    WorkerConfig,
)
from qwen_tts_bridge_worker.engine import (
    EngineFactoryError,
    MockTtsEngine,
    UnsupportedAudioFormatError,
    create_engine,
)
from qwen_tts_bridge_worker.engine.types import AudioFormat, SynthesisRequest


class EngineFactoryTests(unittest.TestCase):
    def test_mock_subcommand_builds_mock_config(self) -> None:
        parser = build_parser()
        args = parser.parse_args(["mock", "--chunks", "2", "--chunk-ms", "40"])

        config = build_engine_config(args)

        self.assertIsInstance(config, MockEngineConfig)
        assert isinstance(config, MockEngineConfig)
        self.assertEqual(2, config.chunk_count)
        self.assertEqual(40, config.chunk_duration_ms)

    def test_qwen_subcommand_is_explicitly_unavailable(self) -> None:
        parser = build_parser()
        args = parser.parse_args(["qwen", "--device", "cuda", "--dtype", "auto"])

        with self.assertRaisesRegex(EngineFactoryError, "not implemented"):
            create_engine(build_engine_config(args))

    def test_legacy_mock_options_still_work(self) -> None:
        parser = build_parser()
        args = parser.parse_args(["--mock", "--mock-chunks", "9"])

        config = build_engine_config(args)

        self.assertIsInstance(config, MockEngineConfig)
        assert isinstance(config, MockEngineConfig)
        self.assertEqual(9, config.chunk_count)

    def test_legacy_mock_options_cannot_be_mixed_with_subcommand(self) -> None:
        parser = build_parser()
        args = parser.parse_args(["--mock-chunks", "9", "mock"])

        with self.assertRaisesRegex(ValueError, "legacy engine flags"):
            build_engine_config(args)

    def test_legacy_qwen_options_cannot_be_mixed_with_subcommand(self) -> None:
        parser = build_parser()
        args = parser.parse_args(["--device", "cpu", "qwen"])

        with self.assertRaisesRegex(ValueError, "legacy engine flags"):
            build_engine_config(args)

    def test_server_options_work_before_subcommand(self) -> None:
        parser = build_parser()
        args = parser.parse_args(
            ["--worker-version", "0.3.0", "--output-queue-size", "256", "mock"]
        )

        config = build_worker_config(args)

        self.assertEqual("0.3.0", config.worker_version)
        self.assertEqual(256, config.output_queue_size)

    def test_server_options_work_after_subcommand(self) -> None:
        parser = build_parser()
        args = parser.parse_args(
            ["mock", "--worker-version", "0.3.0", "--output-queue-size", "256"]
        )

        config = build_worker_config(args)

        self.assertEqual("0.3.0", config.worker_version)
        self.assertEqual(256, config.output_queue_size)

    def test_server_options_cannot_be_repeated_around_subcommand(self) -> None:
        parser = build_parser()
        args = parser.parse_args(
            ["--output-queue-size", "256", "mock", "--output-queue-size", "512"]
        )

        with self.assertRaisesRegex(ValueError, "output-queue-size"):
            build_worker_config(args)

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

    def test_worker_config_rejects_unknown_engine_config_type(self) -> None:
        with self.assertRaises(TypeError):
            WorkerConfig(engine=object())  # type: ignore[arg-type]

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

    def test_reject_too_short_mock_chunk_duration(self) -> None:
        with self.assertRaisesRegex(ValueError, "at least 20"):
            MockEngineConfig(chunk_duration_ms=10)


if __name__ == "__main__":
    unittest.main()
