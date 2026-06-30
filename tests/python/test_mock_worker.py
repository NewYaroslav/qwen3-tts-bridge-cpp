import json
import os
import queue
import subprocess
import sys
import threading
import unittest
from pathlib import Path
from typing import Callable, Optional

from qwen_tts_bridge_worker.protocol import (
    Frame,
    FrameParser,
    FrameType,
    ParseStatus,
    encode_frame,
)
from qwen_tts_bridge_worker.protocol.control import encode_json_payload

ROOT_DIR = Path(__file__).resolve().parents[2]


class WorkerHarness:
    def __init__(
        self,
        extra_args: list[str],
        engine_args: Optional[list[str]] = None,
    ) -> None:
        if engine_args is None:
            engine_args = ["--mock"]
        env = os.environ.copy()
        worker_path = str(ROOT_DIR / "worker" / "src")
        env["PYTHONPATH"] = (
            worker_path
            if not env.get("PYTHONPATH")
            else worker_path + os.pathsep + env["PYTHONPATH"]
        )

        self.process = subprocess.Popen(
            [
                sys.executable,
                "-m",
                "qwen_tts_bridge_worker",
                *engine_args,
                *extra_args,
            ],
            cwd=str(ROOT_DIR),
            env=env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self._frames: queue.Queue[Frame] = queue.Queue()
        self._reader_error: queue.Queue[str] = queue.Queue()
        self._reader = threading.Thread(target=self._read_stdout, daemon=True)
        self._reader.start()

    def send_control(self, request_id: int, message: dict[str, object]) -> None:
        assert self.process.stdin is not None
        self.process.stdin.write(
            encode_frame(
                FrameType.CONTROL_JSON,
                request_id,
                encode_json_payload(message),
            )
        )
        self.process.stdin.flush()

    def read_frame(
        self,
        predicate: Optional[Callable[[Frame], bool]] = None,
        timeout: float = 3.0,
    ) -> Frame:
        while True:
            frame = self._frames.get(timeout=timeout)
            if predicate is None or predicate(frame):
                return frame

    def wait(self, timeout: float = 3.0) -> int:
        exit_code = self.process.wait(timeout=timeout)
        self._reader.join(timeout=1.0)
        self._close_pipes()
        return exit_code

    def close(self) -> None:
        if self.process.poll() is None:
            try:
                self.send_control(0, {"message_type": "shutdown", "mode": "cancel"})
            except (BrokenPipeError, OSError):
                pass
            try:
                self.process.wait(timeout=3.0)
            except subprocess.TimeoutExpired:
                self.process.terminate()
                self.process.wait(timeout=3.0)
        self._reader.join(timeout=1.0)
        self._close_pipes()

    def stderr_text(self) -> str:
        if self.process.stderr is None:
            return ""
        try:
            return self.process.stderr.read().decode("utf-8", errors="replace")
        except ValueError:
            return ""

    def _read_stdout(self) -> None:
        assert self.process.stdout is not None
        parser = FrameParser()
        while True:
            chunk = self.process.stdout.read(1)
            if not chunk:
                return
            parser.append(chunk)
            while True:
                result = parser.parse_next()
                if result.status == ParseStatus.NEED_MORE_DATA:
                    break
                if result.status == ParseStatus.FATAL_ERROR:
                    self._reader_error.put(result.message)
                    return
                if result.frame is not None:
                    self._frames.put(result.frame)

    def _close_pipes(self) -> None:
        for pipe in (self.process.stdin, self.process.stdout, self.process.stderr):
            if pipe is None:
                continue
            try:
                pipe.close()
            except OSError:
                pass


def control_payload(frame: Frame) -> dict[str, object]:
    return json.loads(frame.payload.decode("utf-8"))


def is_control_message(
    frame: Frame,
    message_type: str,
    request_id: Optional[int] = None,
) -> bool:
    if frame.header.frame_type != FrameType.CONTROL_JSON:
        return False
    if request_id is not None and frame.header.request_id != request_id:
        return False
    return control_payload(frame).get("message_type") == message_type


class MockWorkerTests(unittest.TestCase):
    def test_mock_subcommand_starts_worker(self) -> None:
        worker = WorkerHarness(["--chunks", "1"], engine_args=["mock"])
        self.addCleanup(worker.close)

        self._hello(worker)

        worker.send_control(0, {"message_type": "shutdown", "mode": "cancel"})
        shutdown_ack = control_payload(worker.read_frame())

        self.assertEqual("shutdown_ack", shutdown_ack["message_type"])
        self.assertEqual(0, worker.wait())

    def test_engine_mock_alias_starts_worker(self) -> None:
        worker = WorkerHarness(["--mock-chunks", "1"], engine_args=["--engine", "mock"])
        self.addCleanup(worker.close)

        self._hello(worker)

        worker.send_control(0, {"message_type": "shutdown", "mode": "cancel"})
        shutdown_ack = control_payload(worker.read_frame())

        self.assertEqual("shutdown_ack", shutdown_ack["message_type"])
        self.assertEqual(0, worker.wait())

    def test_handshake_ping_shutdown(self) -> None:
        worker = WorkerHarness(["--mock-chunks", "1"])
        self.addCleanup(worker.close)

        worker.send_control(
            0,
            {
                "message_type": "hello",
                "client_name": "test-client",
                "client_version": "0.2.0",
            },
        )
        ready = control_payload(worker.read_frame())

        self.assertEqual("ready", ready["message_type"])
        self.assertEqual("0.2.0", ready["worker_version"])
        self.assertTrue(ready["warmed_up"])

        worker.send_control(0, {"message_type": "ping", "sequence": 17})
        pong = control_payload(worker.read_frame())

        self.assertEqual({"message_type": "pong", "sequence": 17}, pong)

        worker.send_control(0, {"message_type": "shutdown", "mode": "drain"})
        shutdown_error = worker.read_frame(
            lambda frame: frame.header.frame_type == FrameType.ERROR_JSON
        )
        self.assertEqual("invalid_field_type", control_payload(shutdown_error)["code"])

        worker.send_control(0, {"message_type": "ping", "sequence": 18})
        second_pong = control_payload(worker.read_frame())

        self.assertEqual({"message_type": "pong", "sequence": 18}, second_pong)

        worker.send_control(0, {"message_type": "shutdown", "mode": "cancel"})
        shutdown_ack = control_payload(worker.read_frame())

        self.assertEqual("shutdown_ack", shutdown_ack["message_type"])
        self.assertEqual(0, worker.wait())

    def test_synthesize_streams_pcm_and_completed(self) -> None:
        worker = WorkerHarness(["--mock-chunks", "2"])
        self.addCleanup(worker.close)
        self._hello(worker)

        worker.send_control(
            1,
            {
                "message_type": "synthesize",
                "text": "Hello from the mock worker.",
                "language": "English",
                "instruction": "Speak calmly.",
                "output": {
                    "sample_format": "s16le",
                    "sample_rate": 24000,
                    "channels": 1,
                },
            },
        )

        queued = worker.read_frame()
        started = worker.read_frame()
        first_audio = worker.read_frame()
        second_audio = worker.read_frame()
        completed = worker.read_frame()

        self.assertEqual("queued", control_payload(queued)["message_type"])
        self.assertEqual(1, queued.header.request_id)

        started_payload = control_payload(started)
        self.assertEqual("started", started_payload["message_type"])
        self.assertEqual(1, started.header.request_id)
        audio_format = started_payload["audio_format"]
        self.assertIsInstance(audio_format, dict)
        assert isinstance(audio_format, dict)
        self.assertEqual(24000, audio_format["sample_rate"])

        self.assertEqual(FrameType.AUDIO_PCM, first_audio.header.frame_type)
        self.assertEqual(FrameType.AUDIO_PCM, second_audio.header.frame_type)
        self.assertGreater(len(first_audio.payload), 0)
        self.assertGreater(len(second_audio.payload), 0)

        self.assertEqual("completed", control_payload(completed)["message_type"])
        self.assertEqual(1, completed.header.request_id)

    def test_unsupported_audio_format_is_protocol_request_error(self) -> None:
        worker = WorkerHarness(["--mock-chunks", "1"])
        self.addCleanup(worker.close)
        self._hello(worker)

        worker.send_control(
            1,
            {
                "message_type": "synthesize",
                "text": "unsupported output",
                "output": {
                    "sample_format": "s16le",
                    "sample_rate": 48000,
                    "channels": 1,
                },
            },
        )

        error = worker.read_frame(
            lambda frame: frame.header.frame_type == FrameType.ERROR_JSON
        )
        payload = control_payload(error)

        self.assertEqual(1, error.header.request_id)
        self.assertEqual("request_error", payload["category"])
        self.assertEqual("unsupported_audio_format", payload["code"])

    def test_cancel_queued_request(self) -> None:
        worker = WorkerHarness(
            [
                "--mock-chunks",
                "5",
                "--mock-chunk-ms",
                "100",
                "--mock-chunk-delay",
                "0.05",
            ]
        )
        self.addCleanup(worker.close)
        self._hello(worker)

        worker.send_control(1, {"message_type": "synthesize", "text": "first"})
        worker.send_control(2, {"message_type": "synthesize", "text": "second"})

        worker.read_frame(lambda frame: is_control_message(frame, "queued", 1))
        queued_second = worker.read_frame(
            lambda frame: is_control_message(frame, "queued", 2)
        )
        self.assertEqual(2, queued_second.header.request_id)

        worker.send_control(2, {"message_type": "cancel"})
        cancelled = worker.read_frame(
            lambda frame: is_control_message(frame, "cancelled", 2)
        )

        self.assertEqual("cancelled", control_payload(cancelled)["message_type"])

    def _hello(self, worker: WorkerHarness) -> None:
        worker.send_control(
            0,
            {
                "message_type": "hello",
                "client_name": "test-client",
                "client_version": "0.2.0",
            },
        )
        ready = control_payload(worker.read_frame())
        self.assertEqual("ready", ready["message_type"])


if __name__ == "__main__":
    unittest.main()
