"""Structured runtime metrics for worker diagnostics."""

from __future__ import annotations

import json
import threading
import time
from dataclasses import dataclass, field
from typing import TextIO


def monotonic_seconds() -> float:
    """Return a monotonic timestamp suitable for duration measurements."""

    return time.perf_counter()


def elapsed_milliseconds(started_at: float, ended_at: float | None = None) -> float:
    """Return elapsed milliseconds rounded for diagnostic output."""

    if ended_at is None:
        ended_at = monotonic_seconds()
    return round((ended_at - started_at) * 1000.0, 3)


@dataclass(slots=True)
class MetricsWriter:
    """Writes structured worker metrics to a text diagnostics stream."""

    stream: TextIO
    prefix: str = "qtb_metric "
    _lock: threading.Lock = field(
        default_factory=threading.Lock,
        init=False,
        repr=False,
    )

    def emit(self, event: str, **fields: object) -> None:
        """Write one metric event and suppress diagnostics failures."""

        payload = {"event": event, **fields}
        try:
            line = (
                self.prefix
                + json.dumps(
                    payload,
                    ensure_ascii=True,
                    separators=(",", ":"),
                    sort_keys=True,
                )
                + "\n"
            )
            with self._lock:
                self.stream.write(line)
                self.stream.flush()
        except Exception:
            return
