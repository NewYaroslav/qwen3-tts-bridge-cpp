"""Command-line entry point for the Python QwenTTSBridge worker."""

from __future__ import annotations

import argparse
import sys
from typing import Sequence

from qwen_tts_bridge_worker.engine import MockTtsEngine
from qwen_tts_bridge_worker.server import StdioWorkerServer


def main(argv: Sequence[str] | None = None) -> int:
    """Run the worker process."""

    parser = _build_parser()
    args = parser.parse_args(argv)

    if not args.mock:
        parser.error("only --mock is implemented at this stage")

    engine = MockTtsEngine(
        chunk_count=args.mock_chunks,
        chunk_duration_ms=args.mock_chunk_ms,
        chunk_delay_seconds=args.mock_chunk_delay,
    )
    server = StdioWorkerServer(
        input_stream=sys.stdin.buffer,
        output_stream=sys.stdout.buffer,
        error_stream=sys.stderr,
        engine=engine,
        worker_version=args.worker_version,
        output_queue_size=args.output_queue_size,
    )
    return server.run()


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="QwenTTSBridge Python worker")
    parser.add_argument(
        "--mock",
        action="store_true",
        help="Run deterministic mock engine instead of Qwen integration.",
    )
    parser.add_argument("--worker-version", default="0.2.0")
    parser.add_argument("--output-queue-size", type=int, default=128)
    parser.add_argument("--mock-chunks", type=int, default=3)
    parser.add_argument("--mock-chunk-ms", type=int, default=100)
    parser.add_argument("--mock-chunk-delay", type=float, default=0.0)
    return parser


if __name__ == "__main__":
    raise SystemExit(main())
