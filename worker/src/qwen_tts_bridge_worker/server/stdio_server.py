"""Stdio worker server for a local TTS engine."""

from __future__ import annotations

import queue
import threading
import traceback
import uuid
from collections import deque
from dataclasses import dataclass
from typing import Any, BinaryIO, Deque, Optional, TextIO, cast

from qwen_tts_bridge_worker.engine import (
    AudioFormat,
    EngineCapabilities,
    SynthesisRequest,
    TtsEngine,
    UnsupportedAudioFormatError,
)
from qwen_tts_bridge_worker.protocol.control import (
    ControlMessageError,
    control_frame,
    decode_control_payload,
    error_frame,
)
from qwen_tts_bridge_worker.protocol.data import Frame, FrameType, ParseStatus
from qwen_tts_bridge_worker.protocol.framing import FrameParser, encode_frame


@dataclass
class _RequestSlot:
    request: SynthesisRequest
    cancel_event: threading.Event
    state: str = "queued"


class _OutputWriter:
    """Single writer thread that serializes all worker stdout frames."""

    def __init__(self, output: BinaryIO, max_queue_size: int) -> None:
        self._output = output
        self._queue: queue.Queue[Optional[bytes]] = queue.Queue(maxsize=max_queue_size)
        self._thread = threading.Thread(target=self._run, name="qtb-stdout-writer")

    def start(self) -> None:
        self._thread.start()

    def send(self, frame: bytes) -> None:
        self._queue.put(frame)

    def stop_when_drained(self) -> None:
        self._queue.put(None)
        self._thread.join()

    def _run(self) -> None:
        while True:
            frame = self._queue.get()
            if frame is None:
                return
            self._output.write(frame)
            self._output.flush()


class StdioWorkerServer:
    """Persistent worker server that speaks protocol v1 over stdin/stdout."""

    def __init__(
        self,
        input_stream: BinaryIO,
        output_stream: BinaryIO,
        error_stream: TextIO,
        engine: TtsEngine,
        worker_version: str = "0.2.0",
        output_queue_size: int = 128,
        read_chunk_size: int = 4096,
    ) -> None:
        self._input = input_stream
        self._error = error_stream
        self._engine = engine
        self._worker_version = worker_version
        self._read_chunk_size = read_chunk_size

        self._writer = _OutputWriter(output_stream, output_queue_size)
        self._parser = FrameParser()
        self._session_id = uuid.uuid4().hex

        self._condition = threading.Condition()
        self._pending: Deque[int] = deque()
        self._active: dict[int, _RequestSlot] = {}
        self._terminal_request_ids: set[int] = set()

        self._hello_seen = False
        self._warmed_up = False
        self._ready_sent = False
        self._shutdown_requested = False
        self._shutdown_ack_needed = False
        self._shutdown_terminal_events_enqueued = False
        self._fatal_error = False

        self._engine_thread = threading.Thread(
            target=self._run_engine_loop,
            name="qtb-engine",
        )

    def run(self) -> int:
        """Run the server until shutdown, EOF, or a fatal framing error."""

        self._writer.start()
        engine_thread_started = False
        try:
            self._engine.load()
            self._engine.warmup()
            self._warmed_up = True
            self._engine_thread.start()
            engine_thread_started = True
            self._read_loop()
        except Exception:
            self._fatal_error = True
            traceback.print_exc(file=self._error)
        finally:
            self._request_shutdown(send_ack=False)
            if engine_thread_started:
                self._engine_thread.join()
            try:
                self._engine.close()
            except Exception:
                self._fatal_error = True
                traceback.print_exc(file=self._error)
            finally:
                self._writer.stop_when_drained()

        return 1 if self._fatal_error else 0

    def _read_loop(self) -> None:
        while not self._shutdown_requested and not self._fatal_error:
            read1 = getattr(self._input, "read1", None)
            if callable(read1):
                chunk = cast(bytes, read1(self._read_chunk_size))
            else:
                chunk = self._input.read(self._read_chunk_size)
            if not chunk:
                return

            self._parser.append(chunk)
            while True:
                result = self._parser.parse_next()
                if result.status == ParseStatus.NEED_MORE_DATA:
                    break
                if result.status == ParseStatus.FATAL_ERROR:
                    self._fatal_error = True
                    return
                if result.frame is None:
                    self._fatal_error = True
                    return
                if not self._handle_frame(result.frame):
                    return

    def _handle_frame(self, frame: Frame) -> bool:
        if frame.header.frame_type != FrameType.CONTROL_JSON:
            self._send_error(
                frame.header.request_id,
                "protocol_error",
                "invalid_message_direction",
                "worker accepts only client-to-worker control_json frames",
            )
            return True

        try:
            message = decode_control_payload(frame.payload)
        except ControlMessageError as exc:
            self._send_error(0, "protocol_error", exc.code, exc.message)
            return True

        message_type = message["message_type"]
        if message_type not in {"hello", "synthesize", "cancel", "ping", "shutdown"}:
            self._send_error(
                frame.header.request_id,
                "protocol_error",
                "unknown_message_type",
                f"unknown client message_type: {message_type}",
            )
            return True

        if not self._ready_sent and message_type not in {"hello", "ping", "shutdown"}:
            self._send_error(
                frame.header.request_id,
                "protocol_error",
                "invalid_session_state",
                "worker is not ready",
            )
            return True

        if message_type == "hello":
            self._handle_hello(frame.header.request_id)
            return True
        if message_type == "ping":
            self._handle_ping(frame.header.request_id, message)
            return True
        if message_type == "synthesize":
            self._handle_synthesize(frame.header.request_id, message)
            return True
        if message_type == "cancel":
            self._handle_cancel(frame.header.request_id)
            return True
        if message_type == "shutdown":
            return self._handle_shutdown(frame.header.request_id, message)

        return True

    def _handle_hello(self, request_id: int) -> None:
        if request_id != 0 or self._hello_seen or self._shutdown_requested:
            self._send_error(
                request_id,
                "protocol_error",
                "invalid_session_state",
                "hello is invalid in the current session state",
            )
            return

        self._hello_seen = True
        self._ready_sent = True
        self._writer.send(
            control_frame(
                0,
                {
                    "message_type": "ready",
                    "worker_version": self._worker_version,
                    "session_id": self._session_id,
                    "warmed_up": self._warmed_up,
                    "capabilities": _capabilities_payload(
                        self._engine.capabilities,
                    ),
                },
            )
        )

    def _handle_ping(self, request_id: int, message: dict[str, Any]) -> None:
        if request_id != 0:
            self._send_error(
                request_id,
                "protocol_error",
                "invalid_session_state",
                "ping must use request_id = 0",
            )
            return

        response: dict[str, Any] = {"message_type": "pong"}
        if "sequence" in message:
            response["sequence"] = message["sequence"]
        self._writer.send(control_frame(0, response))

    def _handle_synthesize(self, request_id: int, message: dict[str, Any]) -> None:
        if self._shutdown_requested:
            self._send_error(
                request_id,
                "protocol_error",
                "shutdown_in_progress",
                "worker is shutting down",
            )
            return

        if request_id == 0:
            self._send_error(
                0,
                "request_error",
                "missing_required_field",
                "synthesize requires a non-zero request_id",
            )
            return

        request = self._parse_synthesis_request(request_id, message)
        if request is None:
            return

        with self._condition:
            if request_id in self._active or request_id in self._terminal_request_ids:
                duplicate = True
            else:
                duplicate = False
                self._active[request_id] = _RequestSlot(
                    request=request,
                    cancel_event=threading.Event(),
                )
                self._pending.append(request_id)
                position = len(self._pending)

        if duplicate:
            self._send_error(
                request_id,
                "request_error",
                "duplicate_request_id",
                "duplicate active request_id",
            )
            return

        self._writer.send(
            control_frame(
                request_id,
                {
                    "message_type": "queued",
                    "position": position,
                },
            )
        )

        with self._condition:
            self._condition.notify_all()

    def _parse_synthesis_request(
        self,
        request_id: int,
        message: dict[str, Any],
    ) -> Optional[SynthesisRequest]:
        text = message.get("text")
        if not isinstance(text, str):
            self._send_error(
                request_id,
                "request_error",
                "missing_required_field",
                "synthesize.text must be a string",
            )
            return None

        language = message.get("language", "auto")
        speaker = message.get("speaker", "default")
        instruction = message.get("instruction", "")
        output_payload = message.get("output")

        if not isinstance(language, str) or not isinstance(speaker, str):
            self._send_error(
                request_id,
                "request_error",
                "invalid_field_type",
                "language and speaker must be strings",
            )
            return None

        if not isinstance(instruction, str):
            self._send_error(
                request_id,
                "request_error",
                "invalid_field_type",
                "instruction must be a string",
            )
            return None

        if output_payload is not None and not isinstance(output_payload, dict):
            self._send_error(
                request_id,
                "request_error",
                "invalid_field_type",
                "output must be an object",
            )
            return None

        try:
            output = AudioFormat.from_payload(output_payload)
        except (TypeError, ValueError):
            self._send_error(
                request_id,
                "request_error",
                "invalid_field_type",
                "output contains invalid field types",
            )
            return None

        request = SynthesisRequest(
            request_id=request_id,
            text=text,
            language=language,
            speaker=speaker,
            instruction=instruction,
            output=output,
        )
        try:
            self._engine.validate_request(request)
        except UnsupportedAudioFormatError as exc:
            self._send_error(
                request_id,
                "request_error",
                "unsupported_audio_format",
                str(exc),
            )
            return None

        return request

    def _handle_cancel(self, request_id: int) -> None:
        if request_id == 0:
            self._send_error(
                0,
                "request_error",
                "unknown_request_id",
                "cancel requires a non-zero request_id",
            )
            return

        send_cancelled = False
        unknown = False

        with self._condition:
            slot = self._active.get(request_id)
            if slot is None:
                unknown = request_id not in self._terminal_request_ids
            elif slot.state == "queued":
                try:
                    self._pending.remove(request_id)
                except ValueError:
                    slot.state = "running"
                    slot.cancel_event.set()
                else:
                    self._terminalize_locked(request_id)
                    send_cancelled = True
            elif slot.state == "running":
                slot.cancel_event.set()

            self._condition.notify_all()

        if unknown:
            self._send_error(
                request_id,
                "request_error",
                "unknown_request_id",
                "request_id is not active",
            )
        elif send_cancelled:
            self._writer.send(control_frame(request_id, {"message_type": "cancelled"}))

    def _handle_shutdown(self, request_id: int, message: dict[str, Any]) -> bool:
        if request_id != 0:
            self._send_error(
                request_id,
                "protocol_error",
                "invalid_session_state",
                "shutdown must use request_id = 0",
            )
            return True

        mode = message.get("mode", "cancel")
        if mode != "cancel":
            self._send_error(
                0,
                "protocol_error",
                "invalid_field_type",
                "protocol v1 supports only shutdown mode = cancel",
            )
            return True

        self._request_shutdown(send_ack=True)
        return False

    def _request_shutdown(self, send_ack: bool) -> None:
        queued_cancelled_ids: list[int] = []

        with self._condition:
            if self._shutdown_requested:
                self._shutdown_ack_needed = self._shutdown_ack_needed or send_ack
                self._shutdown_terminal_events_enqueued = True
                self._condition.notify_all()
                return

            self._shutdown_requested = True
            self._shutdown_ack_needed = send_ack

            while self._pending:
                request_id = self._pending.popleft()
                slot = self._active.get(request_id)
                if slot is None:
                    continue
                slot.cancel_event.set()
                self._terminalize_locked(request_id)
                queued_cancelled_ids.append(request_id)

            for slot in self._active.values():
                slot.cancel_event.set()

        for request_id in queued_cancelled_ids:
            self._writer.send(control_frame(request_id, {"message_type": "cancelled"}))

        with self._condition:
            self._shutdown_terminal_events_enqueued = True
            self._condition.notify_all()

    def _run_engine_loop(self) -> None:
        while True:
            slot = self._take_next_request()
            if slot is None:
                break
            self._run_one_request(slot)

        if self._shutdown_ack_needed:
            self._writer.send(control_frame(0, {"message_type": "shutdown_ack"}))

    def _take_next_request(self) -> Optional[_RequestSlot]:
        with self._condition:
            while not self._pending:
                if self._shutdown_requested and self._shutdown_terminal_events_enqueued:
                    return None
                self._condition.wait()

            request_id = self._pending.popleft()
            slot = self._active.get(request_id)
            if slot is None:
                return None
            slot.state = "running"
            return slot

    def _run_one_request(self, slot: _RequestSlot) -> None:
        request_id = slot.request.request_id

        if slot.cancel_event.is_set():
            self._finish_cancelled(request_id)
            return

        self._writer.send(
            control_frame(
                request_id,
                {
                    "message_type": "started",
                    "audio_format": slot.request.output.to_payload(),
                },
            )
        )

        try:
            for pcm_chunk in self._engine.synthesize_stream(
                slot.request,
                slot.cancel_event,
            ):
                if slot.cancel_event.is_set():
                    break
                if not pcm_chunk:
                    continue
                self._writer.send(
                    encode_frame(FrameType.AUDIO_PCM, request_id, pcm_chunk)
                )
        except Exception as exc:
            with self._condition:
                self._terminalize_locked(request_id)
            self._send_error(
                request_id,
                "model_error",
                "synthesis_failed",
                str(exc) or "synthesis failed",
            )
            return

        if slot.cancel_event.is_set():
            self._finish_cancelled(request_id)
        else:
            with self._condition:
                self._terminalize_locked(request_id)
            self._writer.send(control_frame(request_id, {"message_type": "completed"}))

    def _finish_cancelled(self, request_id: int) -> None:
        with self._condition:
            self._terminalize_locked(request_id)
        self._writer.send(control_frame(request_id, {"message_type": "cancelled"}))

    def _terminalize_locked(self, request_id: int) -> None:
        self._active.pop(request_id, None)
        self._terminal_request_ids.add(request_id)

    def _send_error(
        self,
        request_id: int,
        category: str,
        code: str,
        message: str,
    ) -> None:
        self._writer.send(error_frame(request_id, category, code, message))


def _capabilities_payload(capabilities: EngineCapabilities) -> dict[str, bool]:
    return {
        "streaming": capabilities.streaming,
        "cancellation": capabilities.cancellation,
        "instructions": capabilities.instructions,
        "voice_clone": capabilities.voice_clone,
    }
