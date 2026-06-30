"""Qwen3-TTS engine adapter.

The adapter keeps heavyweight Qwen/Torch imports out of normal worker startup
until the qwen engine is actually selected and loaded.
"""

from __future__ import annotations

import gc
import importlib
import sys
import threading
from collections.abc import Callable, Iterable, Iterator
from pathlib import Path
from typing import Any

from qwen_tts_bridge_worker.config import QwenEngineConfig
from qwen_tts_bridge_worker.engine.types import (
    AudioFormat,
    EngineCapabilities,
    EngineRequestValidationError,
    SynthesisRequest,
    UnsupportedAudioFormatError,
)

QwenModelLoader = Callable[[QwenEngineConfig], Any]


class QwenEngineError(RuntimeError):
    """Raised when the Qwen adapter cannot load or run the model."""


class QwenTtsEngine:
    """Adapter around the vendored Qwen3-TTS streaming package."""

    def __init__(
        self,
        config: QwenEngineConfig,
        model_loader: QwenModelLoader | None = None,
    ) -> None:
        self._config = config
        self._model_loader = model_loader or _default_model_loader
        self._model: Any | None = None

    @property
    def capabilities(self) -> EngineCapabilities:
        """Return capabilities exposed by the first Qwen adapter pass."""

        return EngineCapabilities(
            streaming=False,
            cancellation=False,
            instructions=True,
            voice_clone=False,
        )

    def load(self) -> None:
        """Load the Qwen model wrapper."""

        if self._model is not None:
            return
        self._model = self._model_loader(self._config)

    def warmup(self) -> None:
        """Ensure the model object exists before the worker sends ready."""

        if self._model is None:
            self.load()

    def validate_request(
        self,
        request: SynthesisRequest,
    ) -> None:
        """Validate output format support."""

        if request.output != AudioFormat.default():
            raise UnsupportedAudioFormatError(
                "qwen engine currently supports only s16le 24000 Hz mono"
            )

        model = self._require_model()
        model_type = _qwen_model_type(model)
        if model_type == "custom_voice":
            _validate_custom_voice_request(model, request)
            return

        if model_type == "voice_design":
            if not request.instruction.strip():
                raise EngineRequestValidationError(
                    "missing_required_field",
                    "qwen voice design model requires an instruction",
                )
            return

        if model_type == "base":
            raise EngineRequestValidationError(
                "missing_required_field",
                "qwen base voice-clone models require reference audio; "
                "the bridge protocol does not support voice clone requests yet",
            )

        raise EngineRequestValidationError(
            "invalid_field_type",
            f"unsupported qwen tts_model_type: {model_type or 'unknown'}",
        )

    def synthesize_stream(
        self,
        request: SynthesisRequest,
        cancel_event: threading.Event,
    ) -> Iterable[bytes]:
        """Generate one full Qwen waveform and expose it as a PCM chunk."""

        if cancel_event.is_set():
            return
        model = self._require_model()
        wavs, sample_rate = self._generate_audio(model, request)
        if sample_rate != request.output.sample_rate:
            raise QwenEngineError(
                "qwen model returned unsupported sample rate "
                f"{sample_rate}, expected {request.output.sample_rate}"
            )

        for wav in wavs:
            if cancel_event.is_set():
                return
            pcm = _float_audio_to_s16le(wav)
            if pcm:
                yield pcm

    def close(self) -> None:
        """Release the loaded model reference."""

        model = self._model
        self._model = None
        close = getattr(model, "close", None)
        if callable(close):
            close()
        gc.collect()

    def _require_model(self) -> Any:
        if self._model is None:
            raise QwenEngineError("qwen model is not loaded")
        return self._model

    def _generate_audio(
        self,
        model: Any,
        request: SynthesisRequest,
    ) -> tuple[Iterable[Any], int]:
        model_type = _qwen_model_type(model)
        language = _qwen_language(request.language)

        if model_type == "custom_voice":
            return model.generate_custom_voice(
                text=request.text,
                language=language,
                speaker=request.speaker,
                instruct=request.instruction or None,
            )

        if model_type == "voice_design":
            return model.generate_voice_design(
                text=request.text,
                language=language,
                instruct=request.instruction,
            )

        if model_type == "base":
            raise EngineRequestValidationError(
                "missing_required_field",
                "qwen base voice-clone models require reference audio; "
                "the bridge protocol does not support voice clone requests yet",
            )

        raise QwenEngineError(
            f"unsupported qwen tts_model_type: {model_type or 'unknown'}"
        )


def _default_model_loader(config: QwenEngineConfig) -> Any:
    _add_default_qwen_package_path()

    try:
        qwen_tts = importlib.import_module("qwen_tts")
        model_cls = qwen_tts.Qwen3TTSModel
    except Exception as exc:
        raise QwenEngineError(
            "failed to import qwen_tts; install the Qwen3-TTS streaming package "
            "or keep external/python/Qwen3-TTS-streaming available"
        ) from exc

    kwargs = _model_load_kwargs(config)
    try:
        return model_cls.from_pretrained(config.model_path, **kwargs)
    except Exception as exc:
        raise QwenEngineError(f"failed to load Qwen model: {exc}") from exc


def _add_default_qwen_package_path() -> None:
    external_path = _repo_root() / "external" / "python" / "Qwen3-TTS-streaming"
    if not external_path.exists():
        return

    path_text = str(external_path)
    if path_text not in sys.path:
        sys.path.insert(0, path_text)


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[4]


def _model_load_kwargs(config: QwenEngineConfig) -> dict[str, Any]:
    kwargs: dict[str, Any] = {"device_map": config.device}

    dtype = _torch_dtype(config.dtype)
    if dtype is not None:
        kwargs["dtype"] = dtype

    if config.attn_implementation:
        kwargs["attn_implementation"] = config.attn_implementation

    return kwargs


def _torch_dtype(dtype_name: str) -> Any | None:
    normalized = dtype_name.strip().lower()
    if normalized == "auto":
        return None

    try:
        torch = importlib.import_module("torch")
    except Exception as exc:
        raise QwenEngineError("torch is required for explicit qwen dtype") from exc

    mapping = {
        "float16": "float16",
        "fp16": "float16",
        "bfloat16": "bfloat16",
        "bf16": "bfloat16",
        "float32": "float32",
        "fp32": "float32",
    }
    attr = mapping.get(normalized)
    if attr is None:
        raise QwenEngineError(f"unsupported qwen dtype: {dtype_name}")
    return getattr(torch, attr)


def _qwen_model_type(model: Any) -> str:
    inner_model = getattr(model, "model", None)
    model_type = getattr(inner_model, "tts_model_type", None)
    if model_type is None:
        model_type = getattr(model, "tts_model_type", "")
    return str(model_type)


def _qwen_language(language: str) -> str | None:
    if language.lower() == "auto":
        return None
    return language


def _validate_custom_voice_request(
    model: Any,
    request: SynthesisRequest,
) -> None:
    if _is_placeholder_speaker(request.speaker):
        raise EngineRequestValidationError(
            "missing_required_field",
            "qwen custom voice model requires an explicit speaker",
        )

    supported_speakers = _supported_speakers(model)
    if supported_speakers is None:
        return

    if request.speaker.lower() not in supported_speakers:
        raise EngineRequestValidationError(
            "invalid_field_type",
            f"qwen custom voice model does not support speaker: {request.speaker}",
        )


def _is_placeholder_speaker(speaker: str) -> bool:
    return not speaker.strip() or speaker.strip().lower() == "default"


def _supported_speakers(model: Any) -> set[str] | None:
    get_supported_speakers = getattr(model, "get_supported_speakers", None)
    if not callable(get_supported_speakers):
        return None

    speakers = get_supported_speakers()
    if speakers is None:
        return None
    if not isinstance(speakers, (list, tuple, set)):
        return None

    return {str(speaker).lower() for speaker in speakers}


def _float_audio_to_s16le(audio: Any) -> bytes:
    try:
        numpy = importlib.import_module("numpy")
    except Exception:
        numpy = None

    if numpy is not None and isinstance(audio, numpy.ndarray):
        clipped = numpy.clip(audio.astype(numpy.float32, copy=False), -1.0, 1.0)
        pcm = (clipped * 32767.0).astype("<i2", copy=False)
        return bytes(pcm.tobytes())

    out = bytearray()
    for sample in _iter_float_samples(audio):
        clipped_sample = max(-1.0, min(1.0, sample))
        value = int(clipped_sample * 32767.0)
        out.extend(value.to_bytes(2, byteorder="little", signed=True))
    return bytes(out)


def _iter_float_samples(audio: Any) -> Iterator[float]:
    if hasattr(audio, "tolist"):
        audio = audio.tolist()

    if isinstance(audio, (int, float)):
        yield float(audio)
        return

    for item in audio:
        if isinstance(item, (list, tuple)):
            yield from _iter_float_samples(item)
        else:
            yield float(item)
