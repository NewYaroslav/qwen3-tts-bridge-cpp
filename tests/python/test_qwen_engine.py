import struct
import threading
import unittest

from qwen_tts_bridge_worker.config import QwenEngineConfig
from qwen_tts_bridge_worker.engine import (
    AudioFormat,
    EngineRequestValidationError,
    QwenTtsEngine,
    SynthesisRequest,
    UnsupportedAudioFormatError,
)


class _InnerModel:
    def __init__(self, model_type: str) -> None:
        self.tts_model_type = model_type


class _CustomVoiceModel:
    def __init__(self) -> None:
        self.model = _InnerModel("custom_voice")
        self.last_call: dict[str, object] | None = None

    def generate_custom_voice(
        self,
        text: str,
        language: str | None,
        speaker: str,
        instruct: str | None,
    ) -> tuple[list[list[float]], int]:
        self.last_call = {
            "text": text,
            "language": language,
            "speaker": speaker,
            "instruct": instruct,
        }
        return [[-1.0, 0.0, 1.0]], 24000

    def get_supported_speakers(self) -> list[str]:
        return ["Alice"]


class _VoiceDesignModel:
    def __init__(self) -> None:
        self.model = _InnerModel("voice_design")
        self.last_call: dict[str, object] | None = None

    def generate_voice_design(
        self,
        text: str,
        language: str | None,
        instruct: str,
    ) -> tuple[list[list[float]], int]:
        self.last_call = {
            "text": text,
            "language": language,
            "instruct": instruct,
        }
        return [[0.25, -0.25]], 24000


class _BaseModel:
    def __init__(self) -> None:
        self.model = _InnerModel("base")


class QwenEngineTests(unittest.TestCase):
    def test_custom_voice_generation_is_mapped_to_pcm(self) -> None:
        fake_model = _CustomVoiceModel()
        engine = QwenTtsEngine(
            QwenEngineConfig(model_path="models/qwen-custom"),
            model_loader=lambda _config: fake_model,
        )
        engine.load()

        chunks = list(
            engine.synthesize_stream(
                SynthesisRequest(
                    request_id=1,
                    text="Hello",
                    language="English",
                    speaker="Alice",
                    instruction="Speak warmly.",
                ),
                threading.Event(),
            )
        )

        self.assertEqual([struct.pack("<hhh", -32767, 0, 32767)], chunks)
        self.assertEqual(
            {
                "text": "Hello",
                "language": "English",
                "speaker": "Alice",
                "instruct": "Speak warmly.",
            },
            fake_model.last_call,
        )

    def test_auto_language_becomes_model_default_language(self) -> None:
        fake_model = _CustomVoiceModel()
        engine = QwenTtsEngine(
            QwenEngineConfig(model_path="models/qwen-custom"),
            model_loader=lambda _config: fake_model,
        )
        engine.load()

        list(
            engine.synthesize_stream(
                SynthesisRequest(
                    request_id=1,
                    text="Hello",
                    language="auto",
                    speaker="Alice",
                ),
                threading.Event(),
            )
        )

        self.assertIsNotNone(fake_model.last_call)
        assert fake_model.last_call is not None
        self.assertIsNone(fake_model.last_call["language"])

    def test_custom_voice_requires_explicit_speaker(self) -> None:
        engine = QwenTtsEngine(
            QwenEngineConfig(model_path="models/qwen-custom"),
            model_loader=lambda _config: _CustomVoiceModel(),
        )
        engine.load()

        for speaker in ("default", ""):
            with self.subTest(speaker=speaker):
                with self.assertRaisesRegex(
                    EngineRequestValidationError,
                    "explicit speaker",
                ):
                    engine.validate_request(
                        SynthesisRequest(
                            request_id=1,
                            text="Hello",
                            speaker=speaker,
                        )
                    )

    def test_custom_voice_rejects_unsupported_speaker(self) -> None:
        engine = QwenTtsEngine(
            QwenEngineConfig(model_path="models/qwen-custom"),
            model_loader=lambda _config: _CustomVoiceModel(),
        )
        engine.load()

        with self.assertRaisesRegex(
            EngineRequestValidationError,
            "does not support speaker",
        ):
            engine.validate_request(
                SynthesisRequest(
                    request_id=1,
                    text="Hello",
                    speaker="Bob",
                )
            )

    def test_voice_design_uses_instruction_as_instruct(self) -> None:
        fake_model = _VoiceDesignModel()
        engine = QwenTtsEngine(
            QwenEngineConfig(model_path="models/qwen-voice-design"),
            model_loader=lambda _config: fake_model,
        )
        engine.load()

        chunks = list(
            engine.synthesize_stream(
                SynthesisRequest(
                    request_id=1,
                    text="Hello",
                    language="English",
                    instruction="Low calm voice.",
                ),
                threading.Event(),
            )
        )

        self.assertEqual([struct.pack("<hh", 8191, -8191)], chunks)
        self.assertEqual(
            {
                "text": "Hello",
                "language": "English",
                "instruct": "Low calm voice.",
            },
            fake_model.last_call,
        )

    def test_voice_design_requires_instruction(self) -> None:
        engine = QwenTtsEngine(
            QwenEngineConfig(model_path="models/qwen-voice-design"),
            model_loader=lambda _config: _VoiceDesignModel(),
        )
        engine.load()

        with self.assertRaisesRegex(
            EngineRequestValidationError,
            "requires an instruction",
        ):
            engine.validate_request(SynthesisRequest(request_id=1, text="Hello"))

    def test_base_voice_clone_is_not_wired_yet(self) -> None:
        engine = QwenTtsEngine(
            QwenEngineConfig(model_path="models/qwen-base"),
            model_loader=lambda _config: _BaseModel(),
        )
        engine.load()

        with self.assertRaisesRegex(EngineRequestValidationError, "voice-clone"):
            engine.validate_request(
                SynthesisRequest(request_id=1, text="Hello"),
            )

    def test_unsupported_audio_format_is_rejected(self) -> None:
        engine = QwenTtsEngine(QwenEngineConfig(model_path="models/qwen"))

        with self.assertRaisesRegex(UnsupportedAudioFormatError, "s16le"):
            engine.validate_request(
                SynthesisRequest(
                    request_id=1,
                    text="Hello",
                    output=AudioFormat(sample_rate=48000),
                )
            )


if __name__ == "__main__":
    unittest.main()
