"""Command-line entry point for the Python QwenTTSBridge worker."""

from __future__ import annotations

import sys
from typing import Sequence

from qwen_tts_bridge_worker.cli import build_parser, build_worker_config
from qwen_tts_bridge_worker.engine import EngineFactoryError, create_engine
from qwen_tts_bridge_worker.server import StdioWorkerServer


def main(argv: Sequence[str] | None = None) -> int:
    """Run the worker process."""

    parser = build_parser()
    args = parser.parse_args(argv)

    try:
        config = build_worker_config(args)
        engine = create_engine(config.engine)
    except (EngineFactoryError, TypeError, ValueError) as exc:
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


if __name__ == "__main__":
    raise SystemExit(main())
