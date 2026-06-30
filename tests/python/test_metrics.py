import json
import threading
import time
import unittest
from typing import TextIO, cast

from qwen_tts_bridge_worker.server.metrics import MetricsWriter


class FragmentingTextStream:
    def __init__(self) -> None:
        self._chunks: list[str] = []
        self._lock = threading.Lock()
        self._writing = False
        self.overlapped_write = False

    def write(self, value: str) -> int:
        with self._lock:
            if self._writing:
                self.overlapped_write = True
            self._writing = True

        split_at = max(1, len(value) // 2)
        try:
            with self._lock:
                self._chunks.append(value[:split_at])
            time.sleep(0.0001)
            with self._lock:
                self._chunks.append(value[split_at:])
        finally:
            with self._lock:
                self._writing = False

        return len(value)

    def flush(self) -> None:
        return

    def getvalue(self) -> str:
        with self._lock:
            return "".join(self._chunks)


class MetricsWriterTests(unittest.TestCase):
    def test_emit_serializes_concurrent_writes(self) -> None:
        stream = FragmentingTextStream()
        writer = MetricsWriter(cast(TextIO, stream))
        start = threading.Barrier(4)

        def emit_many(worker_id: int) -> None:
            start.wait()
            for index in range(100):
                writer.emit(
                    "concurrent_metric",
                    worker_id=worker_id,
                    index=index,
                )

        threads = [
            threading.Thread(target=emit_many, args=(worker_id,))
            for worker_id in range(4)
        ]

        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()

        lines = stream.getvalue().splitlines()

        self.assertFalse(stream.overlapped_write)
        self.assertEqual(400, len(lines))
        for line in lines:
            self.assertTrue(line.startswith("qtb_metric "))
            payload = json.loads(line.removeprefix("qtb_metric "))
            self.assertEqual("concurrent_metric", payload["event"])


if __name__ == "__main__":
    unittest.main()
