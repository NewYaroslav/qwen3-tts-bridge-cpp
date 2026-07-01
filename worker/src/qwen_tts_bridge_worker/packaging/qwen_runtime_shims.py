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


def qwen_transform_get_item_to_index() -> Any:
    """Return a small eager-mode replacement for Transformers' Dynamo helper.

    Recent Transformers versions use Torch Dynamo's ``TransformGetItemToIndex``
    around attention-mask construction. The packaged worker runs eager
    inference and does not need the broader Dynamo/SymPy import graph, but the
    masking helper still expects a context manager with the same tensor
    indexing behavior.
    """

    torch = importlib.import_module("torch")
    pytree = importlib.import_module("torch.utils._pytree")
    torch_overrides = importlib.import_module("torch.overrides")
    TorchFunctionMode = torch_overrides.TorchFunctionMode

    class TransformGetItemToIndex(TorchFunctionMode):  # type: ignore[misc, valid-type]
        def __torch_function__(
            self,
            func: Any,
            types: Any,
            args: tuple[Any, ...] = (),
            kwargs: dict[str, Any] | None = None,
        ) -> Any:
            if kwargs is None:
                kwargs = {}

            if len(args) >= 2 and func is torch.Tensor.__getitem__:
                tensor_to_index = args[0]
                if isinstance(tensor_to_index, torch.Tensor):
                    index_args = pytree.tree_leaves(args[1])
                    if all(
                        isinstance(value, (torch.Tensor, int))
                        for value in index_args
                    ):
                        converted_indices = [
                            torch.tensor(
                                value,
                                dtype=torch.int64,
                                device=tensor_to_index.device,
                            )
                            if isinstance(value, int)
                            else value
                            for value in index_args
                        ]
                        return torch.ops.aten.index(tensor_to_index, converted_indices)

            return func(*args, **kwargs)

    return TransformGetItemToIndex
