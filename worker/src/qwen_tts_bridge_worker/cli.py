"""Command-line parsing and worker configuration construction."""

from __future__ import annotations

import argparse
from typing import TypeVar

from qwen_tts_bridge_worker.config import (
    EngineConfig,
    MockEngineConfig,
    QwenEngineConfig,
    WorkerConfig,
)

T = TypeVar("T")


def build_parser() -> argparse.ArgumentParser:
    """Build the worker command-line parser."""

    parser = argparse.ArgumentParser(description="QwenTTSBridge Python worker")

    _add_root_server_options(parser)

    _add_legacy_engine_options(parser)

    subparsers = parser.add_subparsers(
        dest="engine_command",
        metavar="engine",
    )
    server_options = _server_options_parent_parser()
    _add_mock_subcommand(subparsers, server_options)
    _add_qwen_subcommand(subparsers, server_options)
    return parser


def build_worker_config(args: argparse.Namespace) -> WorkerConfig:
    """Build a validated worker configuration from parsed arguments."""

    return WorkerConfig(
        worker_version=_selected_worker_version(args),
        output_queue_size=_selected_output_queue_size(args),
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
                attn_implementation=args.attn_implementation,
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
            chunk_count=_value_or_default(args.legacy_mock_chunks, 3),
            chunk_duration_ms=_value_or_default(args.legacy_mock_chunk_ms, 100),
            chunk_delay_seconds=_value_or_default(args.legacy_mock_chunk_delay, 0.0),
        )
    if engine_name == "qwen":
        return QwenEngineConfig(
            model_path=_value_or_default(args.legacy_model_path, ""),
            device=_value_or_default(args.legacy_device, "cuda"),
            dtype=_value_or_default(args.legacy_dtype, "auto"),
            attn_implementation=_value_or_default(
                args.legacy_attn_implementation,
                "",
            ),
        )
    raise ValueError(f"unsupported engine: {engine_name}")


def _add_root_server_options(parser: argparse.ArgumentParser) -> None:
    server_group = parser.add_argument_group("server options")
    server_group.add_argument("--worker-version", dest="root_worker_version")
    server_group.add_argument(
        "--output-queue-size",
        dest="root_output_queue_size",
        type=int,
    )


def _server_options_parent_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--worker-version", dest="command_worker_version")
    parser.add_argument(
        "--output-queue-size",
        dest="command_output_queue_size",
        type=int,
    )
    return parser


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
    mock_group.add_argument("--mock-chunks", dest="legacy_mock_chunks", type=int)
    mock_group.add_argument("--mock-chunk-ms", dest="legacy_mock_chunk_ms", type=int)
    mock_group.add_argument(
        "--mock-chunk-delay",
        dest="legacy_mock_chunk_delay",
        type=float,
    )

    qwen_group = parser.add_argument_group("legacy future qwen engine options")
    qwen_group.add_argument("--model-path", dest="legacy_model_path")
    qwen_group.add_argument("--device", dest="legacy_device")
    qwen_group.add_argument("--dtype", dest="legacy_dtype")
    qwen_group.add_argument(
        "--attn-implementation",
        dest="legacy_attn_implementation",
    )


def _add_mock_subcommand(
    subparsers: argparse._SubParsersAction[argparse.ArgumentParser],
    server_options: argparse.ArgumentParser,
) -> None:
    mock_parser = subparsers.add_parser(
        "mock",
        parents=[server_options],
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
    server_options: argparse.ArgumentParser,
) -> None:
    qwen_parser = subparsers.add_parser(
        "qwen",
        parents=[server_options],
        help="Run the Qwen3-TTS engine.",
    )
    qwen_parser.add_argument("--model-path", required=True)
    qwen_parser.add_argument("--device", default="cuda")
    qwen_parser.add_argument("--dtype", default="auto")
    qwen_parser.add_argument("--attn-implementation", default="")


def _reject_mixed_legacy_engine_flags(args: argparse.Namespace) -> None:
    legacy_values = (
        args.mock,
        args.engine is not None,
        args.legacy_mock_chunks is not None,
        args.legacy_mock_chunk_ms is not None,
        args.legacy_mock_chunk_delay is not None,
        args.legacy_model_path is not None,
        args.legacy_device is not None,
        args.legacy_dtype is not None,
        args.legacy_attn_implementation is not None,
    )
    if any(legacy_values):
        raise ValueError(
            "legacy engine flags cannot be combined with engine subcommands"
        )


def _selected_worker_version(args: argparse.Namespace) -> str:
    return _selected_server_option(
        args.root_worker_version,
        getattr(args, "command_worker_version", None),
        "worker-version",
        "0.2.0",
    )


def _selected_output_queue_size(args: argparse.Namespace) -> int:
    return _selected_server_option(
        args.root_output_queue_size,
        getattr(args, "command_output_queue_size", None),
        "output-queue-size",
        128,
    )


def _selected_server_option(
    root_value: T | None,
    command_value: T | None,
    name: str,
    default: T,
) -> T:
    if root_value is not None and command_value is not None:
        raise ValueError(
            f"--{name} cannot be specified both before and after subcommand"
        )
    if command_value is not None:
        return command_value
    if root_value is not None:
        return root_value
    return default


def _value_or_default(value: T | None, default: T) -> T:
    if value is None:
        return default
    return value
