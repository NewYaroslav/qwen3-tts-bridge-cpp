"""Qwen model loading boundary used by the worker engine adapter."""

from __future__ import annotations

import importlib
import sys
from pathlib import Path
from typing import Any

from qwen_tts_bridge_worker.config import QwenEngineConfig


class QwenModelLoadError(RuntimeError):
    """Raised when the bridge cannot import or construct the Qwen model."""


def load_qwen_model(config: QwenEngineConfig) -> Any:
    """Load the Qwen model wrapper from the vendored or installed runtime."""

    add_default_qwen_package_path()

    try:
        qwen_model = importlib.import_module("qwen_tts.inference.qwen3_tts_model")
        model_cls = qwen_model.Qwen3TTSModel
    except Exception as exc:
        raise QwenModelLoadError(
            "failed to import qwen_tts.inference.qwen3_tts_model; install the "
            "Qwen3-TTS streaming package or keep "
            "external/python/Qwen3-TTS-streaming available"
        ) from exc

    kwargs = _model_load_kwargs(config)
    try:
        return model_cls.from_pretrained(config.model_path, **kwargs)
    except Exception as exc:
        raise QwenModelLoadError(f"failed to load Qwen model: {exc}") from exc


def add_default_qwen_package_path() -> None:
    """Prepend the vendored Qwen fork to sys.path when it is available."""

    external_path = _repo_root() / "external" / "python" / "Qwen3-TTS-streaming"
    if not external_path.exists():
        return

    path_text = str(external_path)
    if path_text not in sys.path:
        sys.path.insert(0, path_text)


def _repo_root() -> Path:
    return Path(__file__).resolve().parents[5]


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
        raise QwenModelLoadError("torch is required for explicit qwen dtype") from exc

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
        raise QwenModelLoadError(f"unsupported qwen dtype: {dtype_name}")
    return getattr(torch, attr)
