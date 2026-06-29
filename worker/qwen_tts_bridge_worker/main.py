"""Command-line entry point for the Python QwenTTSBridge worker."""

from __future__ import annotations

import argparse
import sys
from typing import Sequence

from qwen_tts_bridge_worker.config import (
    MockEngineConfig,
    QwenEngineConfig,
    WorkerConfig,
)
from qwen_tts_bridge_worker.engine import EngineFactoryError, create_engine
from qwen_tts_bridge_worker.server import StdioWorkerServer


def main(argv: Sequence[str] | None = None) -> int:
    """Run the worker process."""

    parser = _build_parser()
    args = parser.parse_args(argv)

    try:
        config = _config_from_args(args)
        engine = create_engine(config)
    except (EngineFactoryError, ValueError) as exc:
        parser.error(str(exc))
        raise AssertionError("argparse exits before this point") from exc

    server = StdioWorkerServer(
        input_stream=sys.stdin.buffer,
        output_stream=sys.stdout.buffer,
        error_stream=sys.stderr,
        engine=engine,
        worker_version=config.worker_version,
        output_queue_size=config.output_queue_size,
    )
    return server.run()


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="QwenTTSBridge Python worker")

    server_group = parser.add_argument_group("server options")
    server_group.add_argument("--worker-version", default="0.2.0")
    server_group.add_argument("--output-queue-size", type=int, default=128)

    engine_group = parser.add_argument_group("engine selection")
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

    mock_group = parser.add_argument_group("mock engine options")
    mock_group.add_argument("--mock-chunks", type=int, default=3)
    mock_group.add_argument("--mock-chunk-ms", type=int, default=100)
    mock_group.add_argument("--mock-chunk-delay", type=float, default=0.0)

    qwen_group = parser.add_argument_group("future qwen engine options")
    qwen_group.add_argument("--model-path", default="")
    qwen_group.add_argument("--device", default="cuda")
    qwen_group.add_argument("--dtype", default="auto")
    return parser


def _config_from_args(args: argparse.Namespace) -> WorkerConfig:
    engine_name = args.engine
    if args.mock:
        if engine_name is not None and engine_name != "mock":
            raise ValueError("--mock cannot be combined with --engine qwen")
        engine_name = "mock"
    if engine_name is None:
        raise ValueError("only --mock or --engine mock is implemented at this stage")

    return WorkerConfig(
        worker_version=args.worker_version,
        output_queue_size=args.output_queue_size,
        engine=engine_name,
        mock=MockEngineConfig(
            chunk_count=args.mock_chunks,
            chunk_duration_ms=args.mock_chunk_ms,
            chunk_delay_seconds=args.mock_chunk_delay,
        ),
        qwen=QwenEngineConfig(
            model_path=args.model_path,
            device=args.device,
            dtype=args.dtype,
        ),
    )


if __name__ == "__main__":
    raise SystemExit(main())
