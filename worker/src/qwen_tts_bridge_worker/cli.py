"""Command-line parsing and worker configuration construction."""

from __future__ import annotations

import argparse

from qwen_tts_bridge_worker.config import (
    EngineConfig,
    MockEngineConfig,
    QwenEngineConfig,
    WorkerConfig,
)


def build_parser() -> argparse.ArgumentParser:
    """Build the worker command-line parser."""

    parser = argparse.ArgumentParser(description="QwenTTSBridge Python worker")

    server_group = parser.add_argument_group("server options")
    server_group.add_argument("--worker-version", default="0.2.0")
    server_group.add_argument("--output-queue-size", type=int, default=128)

    _add_legacy_engine_options(parser)

    subparsers = parser.add_subparsers(
        dest="engine_command",
        metavar="engine",
    )
    _add_mock_subcommand(subparsers)
    _add_qwen_subcommand(subparsers)
    return parser


def build_worker_config(args: argparse.Namespace) -> WorkerConfig:
    """Build a validated worker configuration from parsed arguments."""

    return WorkerConfig(
        worker_version=args.worker_version,
        output_queue_size=args.output_queue_size,
        engine=build_engine_config(args),
    )


def build_engine_config(args: argparse.Namespace) -> EngineConfig:
    """Build the selected engine configuration from parsed arguments."""

    engine_command = getattr(args, "engine_command", None)
    if engine_command is not None:
        _reject_mixed_legacy_engine_flags(args)
        if engine_command == "mock":
            return MockEngineConfig(
                chunk_count=args.mock_chunks,
                chunk_duration_ms=args.mock_chunk_ms,
                chunk_delay_seconds=args.mock_chunk_delay,
            )
        if engine_command == "qwen":
            return QwenEngineConfig(
                model_path=args.model_path,
                device=args.device,
                dtype=args.dtype,
            )
        raise ValueError(f"unsupported engine command: {engine_command}")

    engine_name = args.engine
    if args.mock:
        if engine_name is not None and engine_name != "mock":
            raise ValueError("--mock cannot be combined with --engine qwen")
        engine_name = "mock"
    if engine_name is None:
        raise ValueError(
            "choose an engine subcommand or use --mock/--engine mock"
        )

    if engine_name == "mock":
        return MockEngineConfig(
            chunk_count=args.mock_chunks,
            chunk_duration_ms=args.mock_chunk_ms,
            chunk_delay_seconds=args.mock_chunk_delay,
        )
    if engine_name == "qwen":
        return QwenEngineConfig(
            model_path=args.model_path,
            device=args.device,
            dtype=args.dtype,
        )
    raise ValueError(f"unsupported engine: {engine_name}")


def _add_legacy_engine_options(parser: argparse.ArgumentParser) -> None:
    engine_group = parser.add_argument_group("legacy engine selection")
    engine_group.add_argument(
        "--mock",
        action="store_true",
        help="Shortcut for --engine mock.",
    )
    engine_group.add_argument(
        "--engine",
        choices=("mock", "qwen"),
        default=None,
        help="Engine backend to run. Only mock is implemented at this stage.",
    )

    mock_group = parser.add_argument_group("legacy mock engine options")
    mock_group.add_argument("--mock-chunks", type=int, default=3)
    mock_group.add_argument("--mock-chunk-ms", type=int, default=100)
    mock_group.add_argument("--mock-chunk-delay", type=float, default=0.0)

    qwen_group = parser.add_argument_group("legacy future qwen engine options")
    qwen_group.add_argument("--model-path", default="")
    qwen_group.add_argument("--device", default="cuda")
    qwen_group.add_argument("--dtype", default="auto")


def _add_mock_subcommand(
    subparsers: argparse._SubParsersAction[argparse.ArgumentParser],
) -> None:
    mock_parser = subparsers.add_parser(
        "mock",
        help="Run the deterministic mock engine.",
    )
    mock_parser.add_argument(
        "--chunks",
        "--mock-chunks",
        dest="mock_chunks",
        type=int,
        default=3,
    )
    mock_parser.add_argument(
        "--chunk-ms",
        "--mock-chunk-ms",
        dest="mock_chunk_ms",
        type=int,
        default=100,
    )
    mock_parser.add_argument(
        "--chunk-delay",
        "--mock-chunk-delay",
        dest="mock_chunk_delay",
        type=float,
        default=0.0,
    )


def _add_qwen_subcommand(
    subparsers: argparse._SubParsersAction[argparse.ArgumentParser],
) -> None:
    qwen_parser = subparsers.add_parser(
        "qwen",
        help="Run the future Qwen3-TTS engine.",
    )
    qwen_parser.add_argument("--model-path", default="")
    qwen_parser.add_argument("--device", default="cuda")
    qwen_parser.add_argument("--dtype", default="auto")


def _reject_mixed_legacy_engine_flags(args: argparse.Namespace) -> None:
    if args.mock or args.engine is not None:
        raise ValueError(
            "legacy --mock/--engine flags cannot be combined with engine subcommands"
        )
