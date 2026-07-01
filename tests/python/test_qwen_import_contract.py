import ast
import unittest
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[2]
_QWEN_ROOT = _REPO_ROOT / "external" / "python" / "Qwen3-TTS-streaming"
_FILES_WITH_LAZY_AUDIO_REFERENCE_IMPORTS = (
    _QWEN_ROOT / "qwen_tts" / "inference" / "qwen3_tts_model.py",
    _QWEN_ROOT / "qwen_tts" / "inference" / "qwen3_tts_tokenizer.py",
    _QWEN_ROOT / "qwen_tts" / "core" / "models" / "modeling_qwen3_tts.py",
    _QWEN_ROOT
    / "qwen_tts"
    / "core"
    / "tokenizer_25hz"
    / "vq"
    / "speech_vq.py",
)
_AUDIO_REFERENCE_MODULES = ("librosa", "soundfile", "librosa.filters")
_QWEN_CORE_PACKAGE_INIT = _QWEN_ROOT / "qwen_tts" / "core" / "__init__.py"
_QWEN_CORE_MODELS_PACKAGE_INIT = (
    _QWEN_ROOT / "qwen_tts" / "core" / "models" / "__init__.py"
)


class QwenImportContractTests(unittest.TestCase):
    def test_audio_reference_dependencies_are_not_top_level_imports(self) -> None:
        for path in _FILES_WITH_LAZY_AUDIO_REFERENCE_IMPORTS:
            with self.subTest(path=path.relative_to(_REPO_ROOT)):
                tree = ast.parse(path.read_text(encoding="utf-8"))
                imported = _top_level_imports(tree)

                for module_name in _AUDIO_REFERENCE_MODULES:
                    self.assertNotIn(module_name, imported)

    def test_qwen_package_exports_do_not_eagerly_import_runtime_models(self) -> None:
        package_imports = {
            _QWEN_CORE_PACKAGE_INIT: (
                "tokenizer_25hz.modeling_qwen3_tts_tokenizer_v1",
                "tokenizer_12hz.modeling_qwen3_tts_tokenizer_v2",
            ),
            _QWEN_CORE_MODELS_PACKAGE_INIT: (
                "modeling_qwen3_tts",
                "processing_qwen3_tts",
            ),
        }

        for path, forbidden_imports in package_imports.items():
            with self.subTest(path=path.relative_to(_REPO_ROOT)):
                tree = ast.parse(path.read_text(encoding="utf-8"))
                imported = _top_level_imports(tree)

                for module_name in forbidden_imports:
                    self.assertNotIn(module_name, imported)


def _top_level_imports(tree: ast.Module) -> set[str]:
    imports: set[str] = set()
    for node in tree.body:
        if isinstance(node, ast.Import):
            imports.update(alias.name for alias in node.names)
        elif isinstance(node, ast.ImportFrom):
            base = node.module or ""
            relative_base = f"{'.' * node.level}{base}"
            normalized_base = relative_base.lstrip(".")

            if normalized_base:
                imports.add(normalized_base)

            for alias in node.names:
                if alias.name == "*":
                    if normalized_base:
                        imports.add(f"{normalized_base}.*")
                    continue

                if normalized_base:
                    imports.add(f"{normalized_base}.{alias.name}")
                else:
                    imports.add(alias.name)
    return imports


if __name__ == "__main__":
    unittest.main()
