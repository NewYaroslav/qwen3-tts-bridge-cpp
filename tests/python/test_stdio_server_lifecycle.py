import io
import json
import threading
import unittest
from collections.abc import Iterable

from qwen_tts_bridge_worker.engine import EngineRequestValidationError
from qwen_tts_bridge_worker.engine.types import (
    EngineCapabilities,
    SynthesisRequest,
)
from qwen_tts_bridge_worker.protocol import (
    Frame,
    FrameParser,
    FrameType,
    ParseStatus,
    encode_frame,
)
from qwen_tts_bridge_worker.protocol.control import encode_json_payload
from qwen_tts_bridge_worker.server import StdioWorkerServer


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


class RequestValidationEngine:
    @property
    def capabilities(self) -> EngineCapabilities:
        return EngineCapabilities(
            streaming=False,
            cancellation=False,
            instructions=True,
            voice_clone=False,
        )

    def load(self) -> None:
        pass

    def warmup(self) -> None:
        pass

    def validate_request(
        self,
        request: SynthesisRequest,
    ) -> None:
        del request
        raise EngineRequestValidationError(
            "missing_required_field",
            "speaker is required",
        )

    def synthesize_stream(
        self,
        request: SynthesisRequest,
        cancel_event: threading.Event,
    ) -> Iterable[bytes]:
        del request, cancel_event
        raise AssertionError("invalid request must not reach synthesis")

    def close(self) -> None:
        pass


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

    def test_engine_request_validation_error_is_request_error(self) -> None:
        input_stream = io.BytesIO(
            _control_frame(
                0,
                {
                    "message_type": "hello",
                    "client_name": "test-client",
                    "client_version": "0.2.0",
                },
            )
            + _control_frame(
                1,
                {
                    "message_type": "synthesize",
                    "text": "Hello",
                },
            )
        )
        output_stream = io.BytesIO()
        server = StdioWorkerServer(
            input_stream=input_stream,
            output_stream=output_stream,
            error_stream=io.StringIO(),
            engine=RequestValidationEngine(),
        )

        exit_code = server.run()
        frames = _parse_frames(output_stream.getvalue())

        self.assertEqual(0, exit_code)
        self.assertEqual("ready", _payload(frames[0])["message_type"])
        self.assertEqual(FrameType.ERROR_JSON, frames[1].header.frame_type)
        self.assertEqual(1, frames[1].header.request_id)
        self.assertEqual(
            {
                "message_type": "error",
                "category": "request_error",
                "code": "missing_required_field",
                "message": "speaker is required",
            },
            _payload(frames[1]),
        )


def _control_frame(request_id: int, message: dict[str, object]) -> bytes:
    return encode_frame(
        FrameType.CONTROL_JSON,
        request_id,
        encode_json_payload(message),
    )


def _parse_frames(data: bytes) -> list[Frame]:
    parser = FrameParser()
    parser.append(data)
    frames: list[Frame] = []
    while True:
        result = parser.parse_next()
        if result.status == ParseStatus.NEED_MORE_DATA:
            return frames
        if result.status != ParseStatus.FRAME_READY or result.frame is None:
            raise AssertionError(f"unexpected parser result: {result}")
        frames.append(result.frame)


def _payload(frame: Frame) -> dict[str, object]:
    return json.loads(frame.payload.decode("utf-8"))


if __name__ == "__main__":
    unittest.main()
