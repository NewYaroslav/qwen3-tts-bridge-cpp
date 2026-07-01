import importlib
import importlib.util
import unittest
from typing import Any, cast

from qwen_tts_bridge_worker.packaging.qwen_runtime_shims import (
    qwen_torchaudio_mel_filter,
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


if __name__ == "__main__":
    unittest.main()
