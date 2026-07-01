"""Probe which heavy modules are loaded by selected Qwen imports."""

from __future__ import annotations

import argparse
import importlib
import json
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

_QWEN_TARGETS = {
    "model": "qwen_tts.inference.qwen3_tts_model",
    "tokenizer": "qwen_tts.inference.qwen3_tts_tokenizer",
    "modeling": "qwen_tts.core.models.modeling_qwen3_tts",
    "speech-vq": "qwen_tts.core.tokenizer_25hz.vq.speech_vq",
}

_DEFAULT_FORBIDDEN_PREFIXES = ("librosa", "soundfile")


@dataclass(frozen=True, slots=True)
class ImportProbeResult:
    """Result of one import probe."""

    target: str
    module: str
    imported: bool
    newly_loaded_forbidden: list[str]
    error: str = ""

    @property
    def ok(self) -> bool:
        """Return true when the module imported without forbidden modules."""

        return self.imported and not self.newly_loaded_forbidden


def probe_import(
    *,
    target: str,
    module: str,
    forbidden_prefixes: tuple[str, ...] = _DEFAULT_FORBIDDEN_PREFIXES,
) -> ImportProbeResult:
    """Import one module and report newly loaded forbidden dependencies."""

    before = set(sys.modules)
    try:
        importlib.import_module(module)
    except Exception as exc:
        return ImportProbeResult(
            target=target,
            module=module,
            imported=False,
            newly_loaded_forbidden=[],
            error=f"{type(exc).__name__}: {exc}",
        )

    after = set(sys.modules)
    newly_loaded = sorted(after - before)
    forbidden = [
        name
        for name in newly_loaded
        if any(
            name == prefix or name.startswith(f"{prefix}.")
            for prefix in forbidden_prefixes
        )
    ]
    return ImportProbeResult(
        target=target,
        module=module,
        imported=True,
        newly_loaded_forbidden=forbidden,
    )


def run_probe(
    targets: list[str],
    forbidden_prefixes: tuple[str, ...] = _DEFAULT_FORBIDDEN_PREFIXES,
) -> list[ImportProbeResult]:
    """Run import probes for the requested target names."""

    _add_worker_src_to_path()
    from qwen_tts_bridge_worker.engine.qwen.model_loader import (  # noqa: PLC0415
        add_default_qwen_package_path,
    )

    add_default_qwen_package_path()
    resolved_targets = list(_QWEN_TARGETS) if "all" in targets else targets
    return [
        probe_import(
            target=target,
            module=_QWEN_TARGETS[target],
            forbidden_prefixes=forbidden_prefixes,
        )
        for target in resolved_targets
    ]


def main(argv: list[str] | None = None) -> int:
    """CLI entry point for the import probe."""

    parser = argparse.ArgumentParser(
        description="Probe Qwen imports for eager audio-reference dependencies."
    )
    parser.add_argument(
        "targets",
        nargs="*",
        choices=("all", *_QWEN_TARGETS),
        default=["all"],
        help="Qwen import targets to probe.",
    )
    parser.add_argument(
        "--forbid",
        action="append",
        dest="forbidden_prefixes",
        default=None,
        help="Module prefix that must not be newly imported.",
    )
    args = parser.parse_args(argv)

    targets = [str(target) for target in args.targets]
    forbidden_prefixes = tuple(
        str(prefix)
        for prefix in args.forbidden_prefixes or _DEFAULT_FORBIDDEN_PREFIXES
    )
    results = run_probe(targets, forbidden_prefixes)
    print(
        json.dumps(
            [asdict(result) | {"ok": result.ok} for result in results],
            ensure_ascii=True,
            indent=2,
            sort_keys=True,
        )
    )
    return 0 if all(result.ok for result in results) else 1


def _add_worker_src_to_path() -> None:
    repo_root = Path(__file__).resolve().parents[2]
    worker_src = repo_root / "worker" / "src"
    if worker_src.exists():
        worker_src_text = str(worker_src)
        if worker_src_text not in sys.path:
            sys.path.insert(0, worker_src_text)


if __name__ == "__main__":
    raise SystemExit(main())
