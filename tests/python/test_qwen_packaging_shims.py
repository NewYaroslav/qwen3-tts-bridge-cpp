import importlib
import importlib.util
import unittest
from typing import Any, cast

from qwen_tts_bridge_worker.packaging.qwen_runtime_shims import (
    qwen_torchaudio_mel_filter,
    qwen_transform_get_item_to_index,
)


def _has_module(name: str) -> bool:
    return importlib.util.find_spec(name) is not None


@unittest.skipUnless(
    _has_module("librosa") and _has_module("torchaudio"),
    "librosa and torchaudio are required for packaging shim comparison",
)
class QwenPackagingShimTest(unittest.TestCase):
    def test_torchaudio_mel_filter_matches_librosa(self) -> None:
        librosa_filters = cast(Any, importlib.import_module("librosa.filters"))
        np = cast(Any, importlib.import_module("numpy"))

        cases = (
            {
                "sr": 16_000,
                "n_fft": 400,
                "n_mels": 80,
                "fmin": 0.0,
                "fmax": 8_000.0,
                "htk": False,
                "norm": "slaney",
                "dtype": np.float32,
            },
            {
                "sr": 22_050,
                "n_fft": 1_024,
                "n_mels": 80,
                "fmin": 20.0,
                "fmax": 8_000.0,
                "htk": False,
                "norm": None,
                "dtype": np.float64,
            },
            {
                "sr": 24_000,
                "n_fft": 1_024,
                "n_mels": 100,
                "fmin": 0.0,
                "fmax": None,
                "htk": True,
                "norm": None,
                "dtype": np.float32,
            },
        )

        for case in cases:
            with self.subTest(case=case):
                expected = librosa_filters.mel(**case)
                actual = qwen_torchaudio_mel_filter(**case)

                self.assertEqual(actual.dtype, expected.dtype)
                np.testing.assert_allclose(actual, expected, rtol=1e-5, atol=1e-5)


@unittest.skipUnless(
    _has_module("torch")
    and _has_module("torch._dynamo._trace_wrapped_higher_order_op")
    and _has_module("transformers"),
    "torch and transformers are required for masking shim comparison",
)
class QwenPackagingMaskingShimTest(unittest.TestCase):
    def test_transform_get_item_to_index_matches_upstream_mask(self) -> None:
        masking_utils = cast(Any, importlib.import_module("transformers.masking_utils"))
        torch = cast(Any, importlib.import_module("torch"))
        torch_dynamo = cast(
            Any,
            importlib.import_module("torch._dynamo._trace_wrapped_higher_order_op"),
        )

        upstream_context = torch_dynamo.TransformGetItemToIndex
        shim_context = qwen_transform_get_item_to_index()

        expected = self._build_recent_sdpa_mask(masking_utils, torch, upstream_context)
        actual = self._build_recent_sdpa_mask(masking_utils, torch, shim_context)

        torch.testing.assert_close(actual, expected)

    def _build_recent_sdpa_mask(
        self,
        masking_utils: Any,
        torch: Any,
        transform_context: Any,
    ) -> Any:
        previous_context = masking_utils.TransformGetItemToIndex
        masking_utils.TransformGetItemToIndex = transform_context
        try:
            return masking_utils.sdpa_mask_recent_torch(
                batch_size=2,
                cache_position=torch.arange(4),
                kv_length=4,
                attention_mask=None,
                allow_is_causal_skip=False,
            )
        finally:
            masking_utils.TransformGetItemToIndex = previous_context


if __name__ == "__main__":
    unittest.main()
