import io
import sys
import threading
import unittest
from collections.abc import Iterable
from pathlib import Path


ROOT_DIR = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT_DIR / "worker"))

from qwen_tts_bridge_worker.engine.types import (  # noqa: E402
    EngineCapabilities,
    SynthesisRequest,
)
from qwen_tts_bridge_worker.server import StdioWorkerServer  # noqa: E402


class FailingLoadEngine:
    def __init__(self) -> None:
        self.close_called = False

    @property
    def capabilities(self) -> EngineCapabilities:
        return EngineCapabilities(
            streaming=False,
            cancellation=False,
            instructions=False,
            voice_clone=False,
        )

    def load(self) -> None:
        raise RuntimeError("load failed")

    def warmup(self) -> None:
        raise AssertionError("warmup must not run after load failure")

    def validate_request(
        self,
        request: SynthesisRequest,
    ) -> None:
        del request

    def synthesize_stream(
        self,
        request: SynthesisRequest,
        cancel_event: threading.Event,
    ) -> Iterable[bytes]:
        del request, cancel_event
        raise AssertionError("synthesize_stream must not run after load failure")

    def close(self) -> None:
        self.close_called = True


class FailingWarmupEngine:
    def __init__(self) -> None:
        self.close_called = False

    @property
    def capabilities(self) -> EngineCapabilities:
        return EngineCapabilities(
            streaming=False,
            cancellation=False,
            instructions=False,
            voice_clone=False,
        )

    def load(self) -> None:
        pass

    def warmup(self) -> None:
        raise RuntimeError("warmup failed")

    def validate_request(
        self,
        request: SynthesisRequest,
    ) -> None:
        del request

    def synthesize_stream(
        self,
        request: SynthesisRequest,
        cancel_event: threading.Event,
    ) -> Iterable[bytes]:
        del request, cancel_event
        raise AssertionError("synthesize_stream must not run after warmup failure")

    def close(self) -> None:
        self.close_called = True


class StdioWorkerServerLifecycleTests(unittest.TestCase):
    def test_load_failure_does_not_join_unstarted_engine_thread(self) -> None:
        engine = FailingLoadEngine()
        stderr = io.StringIO()
        server = StdioWorkerServer(
            input_stream=io.BytesIO(),
            output_stream=io.BytesIO(),
            error_stream=stderr,
            engine=engine,
        )

        exit_code = server.run()

        self.assertEqual(1, exit_code)
        self.assertTrue(engine.close_called)
        self.assertIn("load failed", stderr.getvalue())
        self.assertNotIn("cannot join thread before it is started", stderr.getvalue())

    def test_warmup_failure_does_not_join_unstarted_engine_thread(self) -> None:
        engine = FailingWarmupEngine()
        stderr = io.StringIO()
        server = StdioWorkerServer(
            input_stream=io.BytesIO(),
            output_stream=io.BytesIO(),
            error_stream=stderr,
            engine=engine,
        )

        exit_code = server.run()

        self.assertEqual(1, exit_code)
        self.assertTrue(engine.close_called)
        self.assertIn("warmup failed", stderr.getvalue())
        self.assertNotIn("cannot join thread before it is started", stderr.getvalue())


if __name__ == "__main__":
    unittest.main()
