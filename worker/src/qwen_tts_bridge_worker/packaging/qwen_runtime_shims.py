"""Runtime shims used by the Qwen Nuitka packaging profile."""

from __future__ import annotations

import importlib
from typing import Any


def qwen_torchaudio_mel_filter(
    *,
    sr: int,
    n_fft: int,
    n_mels: int = 128,
    fmin: float = 0.0,
    fmax: float | None = None,
    htk: bool = False,
    norm: str | None = "slaney",
    dtype: Any = None,
    **_: Any,
) -> Any:
    """Return a librosa-compatible mel filter bank using torchaudio.

    The Qwen CustomVoice and VoiceDesign packaging profiles do not need the
    broader librosa/SciPy/joblib graph. Torchaudio can produce the same mel
    filter matrix for the parameters used by Qwen, while already being part of
    the runtime dependency graph.
    """

    torchaudio_functional = importlib.import_module("torchaudio.functional")

    if fmax is None:
        fmax = sr / 2.0

    mel_basis = torchaudio_functional.melscale_fbanks(
        n_freqs=n_fft // 2 + 1,
        f_min=fmin,
        f_max=fmax,
        n_mels=n_mels,
        sample_rate=sr,
        norm=norm,
        mel_scale="htk" if htk else "slaney",
    ).transpose(0, 1).numpy()

    if dtype is not None:
        return mel_basis.astype(dtype, copy=False)

    return mel_basis
