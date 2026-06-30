"""Smoke-test a packaged QwenTTSBridge worker executable."""

from __future__ import annotations

import argparse
import json
import queue
import subprocess
import threading
from pathlib import Path
from typing import Callable

from qwen_tts_bridge_worker.protocol import (
    Frame,
    FrameParser,
    FrameType,
    ParseStatus,
    encode_frame,
)
from qwen_tts_bridge_worker.protocol.control import encode_json_payload


class PackagedWorkerHarness:
    """Small protocol harness for a packaged worker executable."""

    def __init__(
        self,
        worker_executable: Path,
        args: list[str],
        timeout_seconds: float,
    ) -> None:
        self._timeout_seconds = timeout_seconds
        self._process = subprocess.Popen(
            [str(worker_executable), *args],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        self._frames: queue.Queue[Frame] = queue.Queue()
        self._reader_errors: queue.Queue[str] = queue.Queue()
        self._stderr_chunks: list[bytes] = []
        self._stderr_lock = threading.Lock()
        self._reader = threading.Thread(target=self._read_stdout, daemon=True)
        self._stderr_reader = threading.Thread(
            target=self._read_stderr,
            daemon=True,
        )
        self._reader.start()
        self._stderr_reader.start()

    def send_control(self, request_id: int, message: dict[str, object]) -> None:
        """Send one control JSON frame."""

        if self._process.stdin is None:
            raise RuntimeError("worker stdin is not available")
        self._process.stdin.write(
            encode_frame(
                FrameType.CONTROL_JSON,
                request_id,
                encode_json_payload(message),
            )
        )
        self._process.stdin.flush()

    def read_frame(
        self,
        predicate: Callable[[Frame], bool] | None = None,
    ) -> Frame:
        """Read the next matching protocol frame."""

        while True:
            self._raise_reader_error_if_any()
            try:
                frame = self._frames.get(timeout=self._timeout_seconds)
            except queue.Empty as exc:
                self._raise_reader_error_if_any()
                if self._process.poll() is not None:
                    self._join_reader_threads()
                    stderr = self.stderr_text()
                    raise RuntimeError(
                        "packaged worker exited before expected frame"
                        + (f"; stderr:\n{stderr}" if stderr else "")
                    ) from exc
                raise RuntimeError(
                    "timed out waiting for packaged worker frame"
                ) from exc

            if predicate is None or predicate(frame):
                return frame
            if frame.header.frame_type == FrameType.ERROR_JSON:
                payload = _json_payload(frame)
                raise RuntimeError(
                    "packaged worker returned error frame while waiting for "
                    f"expected frame: {payload}"
                )

    def wait(self) -> int:
        """Wait for the process and close pipes."""

        try:
            exit_code = self._process.wait(timeout=self._timeout_seconds)
        except subprocess.TimeoutExpired as exc:
            self._process.terminate()
            exit_code = self._process.wait(timeout=self._timeout_seconds)
            raise RuntimeError(
                "packaged worker did not exit before timeout"
            ) from exc
        finally:
            self._join_reader_threads()
            self._close_pipes()

        return exit_code

    def close(self) -> None:
        """Best-effort worker cleanup."""

        if self._process.poll() is None:
            try:
                self.send_control(0, {"message_type": "shutdown", "mode": "cancel"})
            except (BrokenPipeError, OSError, RuntimeError):
                pass
            try:
                self._process.wait(timeout=self._timeout_seconds)
            except subprocess.TimeoutExpired:
                self._process.terminate()
                self._process.wait(timeout=self._timeout_seconds)
        self._join_reader_threads()
        self._close_pipes()

    def stderr_text(self) -> str:
        """Return captured stderr as UTF-8 text."""

        with self._stderr_lock:
            return b"".join(self._stderr_chunks).decode(
                "utf-8",
                errors="replace",
            )

    def _read_stdout(self) -> None:
        if self._process.stdout is None:
            self._reader_errors.put("worker stdout is not available")
            return

        parser = FrameParser()
        while True:
            chunk = self._process.stdout.read(1)
            if not chunk:
                return
            parser.append(chunk)
            while True:
                result = parser.parse_next()
                if result.status == ParseStatus.NEED_MORE_DATA:
                    break
                if result.status == ParseStatus.FATAL_ERROR:
                    self._reader_errors.put(result.message)
                    return
                if result.frame is not None:
                    self._frames.put(result.frame)

    def _read_stderr(self) -> None:
        if self._process.stderr is None:
            return

        while True:
            try:
                chunk = self._process.stderr.read(4096)
            except ValueError:
                return
            if not chunk:
                return
            with self._stderr_lock:
                self._stderr_chunks.append(chunk)

    def _join_reader_threads(self) -> None:
        self._reader.join(timeout=1.0)
        self._stderr_reader.join(timeout=1.0)

    def _close_pipes(self) -> None:
        for pipe in (
            self._process.stdin,
            self._process.stdout,
            self._process.stderr,
        ):
            if pipe is None:
                continue
            try:
                pipe.close()
            except OSError:
                pass

    def _raise_reader_error_if_any(self) -> None:
        try:
            message = self._reader_errors.get_nowait()
        except queue.Empty:
            return
        raise RuntimeError(f"packaged worker stdout parser failed: {message}")


def main() -> int:
    """Run the packaged worker smoke test."""

    parser = argparse.ArgumentParser()
    parser.add_argument("worker_executable", type=Path)
    parser.add_argument("--engine", choices=("mock", "qwen"), default="mock")
    parser.add_argument("--model-path")
    parser.add_argument("--device", default="cuda")
    parser.add_argument("--dtype", default="auto")
    parser.add_argument("--attn-implementation", default="")
    parser.add_argument("--timeout-seconds", type=float, default=20.0)
    parser.add_argument("--mock-chunks", type=int, default=1)
    parser.add_argument("--text", default="Packaged worker smoke test.")
    parser.add_argument("--language", default="auto")
    parser.add_argument("--speaker", default="")
    parser.add_argument("--instruction", default="")
    args = parser.parse_args()

    worker_executable = args.worker_executable.resolve()
    if not worker_executable.is_file():
        parser.error(f"worker executable was not found: {worker_executable}")
    if args.engine == "qwen" and not args.model_path:
        parser.error("--model-path is required for --engine qwen")

    harness = PackagedWorkerHarness(
        worker_executable=worker_executable,
        args=_worker_args(args),
        timeout_seconds=args.timeout_seconds,
    )
    try:
        _exercise_worker(
            harness,
            text=args.text,
            language=args.language,
            speaker=args.speaker,
            instruction=args.instruction,
        )
    finally:
        harness.close()

    print(f"packaged worker smoke test passed: {worker_executable}")
    return 0


def _worker_args(args: argparse.Namespace) -> list[str]:
    engine = str(args.engine)
    if engine == "mock":
        return ["mock", "--chunks", str(args.mock_chunks)]

    model_path = args.model_path
    if not isinstance(model_path, str) or not model_path:
        raise RuntimeError("--model-path is required for --engine qwen")

    worker_args = [
        "qwen",
        "--model-path",
        model_path,
        "--device",
        str(args.device),
        "--dtype",
        str(args.dtype),
    ]
    attn_implementation = str(args.attn_implementation)
    if attn_implementation:
        worker_args.extend(["--attn-implementation", attn_implementation])
    return worker_args


def _exercise_worker(
    harness: PackagedWorkerHarness,
    text: str,
    language: str,
    speaker: str,
    instruction: str,
) -> None:
    harness.send_control(
        0,
        {
            "message_type": "hello",
            "client_name": "packaged-worker-smoke",
            "client_version": "0.2.0",
        },
    )
    ready = _control_payload(
        harness.read_frame(lambda frame: _is_control_message(frame, "ready", 0))
    )
    _expect(ready.get("warmed_up") is True, "worker did not report warmed_up")

    harness.send_control(
        1,
        _synthesize_payload(
            text=text,
            language=language,
            speaker=speaker,
            instruction=instruction,
        ),
    )

    harness.read_frame(lambda frame: _is_control_message(frame, "queued", 1))
    harness.read_frame(lambda frame: _is_control_message(frame, "started", 1))
    audio = harness.read_frame(
        lambda frame: frame.header.frame_type == FrameType.AUDIO_PCM
        and frame.header.request_id == 1
    )
    _expect(len(audio.payload) > 0, "packaged worker produced an empty PCM frame")
    harness.read_frame(lambda frame: _is_control_message(frame, "completed", 1))

    harness.send_control(0, {"message_type": "shutdown", "mode": "cancel"})
    harness.read_frame(lambda frame: _is_control_message(frame, "shutdown_ack", 0))
    exit_code = harness.wait()
    _expect(exit_code == 0, f"packaged worker exited with code {exit_code}")


def _synthesize_payload(
    text: str,
    language: str,
    speaker: str,
    instruction: str,
) -> dict[str, object]:
    payload: dict[str, object] = {
        "message_type": "synthesize",
        "text": text,
        "language": language,
        "output": {
            "sample_format": "s16le",
            "sample_rate": 24000,
            "channels": 1,
        },
    }
    if speaker:
        payload["speaker"] = speaker
    if instruction:
        payload["instruction"] = instruction
    return payload


def _is_control_message(
    frame: Frame,
    message_type: str,
    request_id: int,
) -> bool:
    if frame.header.frame_type != FrameType.CONTROL_JSON:
        return False
    if frame.header.request_id != request_id:
        return False
    return _control_payload(frame).get("message_type") == message_type


def _control_payload(frame: Frame) -> dict[str, object]:
    return _json_payload(frame)


def _json_payload(frame: Frame) -> dict[str, object]:
    return json.loads(frame.payload.decode("utf-8"))


def _expect(condition: bool, message: str) -> None:
    if not condition:
        raise RuntimeError(message)


if __name__ == "__main__":
    raise SystemExit(main())
