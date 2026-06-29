"""Small RIFF/WAVE verifier used by CTest example coverage."""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Verify a PCM WAV file header.")
    parser.add_argument("path")
    parser.add_argument("--sample-rate", type=int, required=True)
    parser.add_argument("--channels", type=int, required=True)
    parser.add_argument("--bits-per-sample", type=int, required=True)
    parser.add_argument("--min-data-bytes", type=int, default=1)
    args = parser.parse_args(argv)

    path = Path(args.path)
    data = path.read_bytes()
    if len(data) < 44:
        raise ValueError(f"WAV file is too small: {len(data)} bytes")

    riff, riff_size, wave, fmt_tag, fmt_size = struct.unpack_from("<4sI4s4sI", data, 0)
    audio_format, channels, sample_rate, byte_rate, block_align, bits = (
        struct.unpack_from("<HHIIHH", data, 20)
    )
    data_tag, data_size = struct.unpack_from("<4sI", data, 36)

    expected_block_align = args.channels * args.bits_per_sample // 8
    expected_byte_rate = args.sample_rate * expected_block_align

    checks = [
        (riff == b"RIFF", "missing RIFF tag"),
        (wave == b"WAVE", "missing WAVE tag"),
        (fmt_tag == b"fmt ", "missing fmt chunk"),
        (fmt_size == 16, "unexpected fmt chunk size"),
        (audio_format == 1, "WAV is not PCM"),
        (channels == args.channels, "unexpected channel count"),
        (sample_rate == args.sample_rate, "unexpected sample rate"),
        (byte_rate == expected_byte_rate, "unexpected byte rate"),
        (block_align == expected_block_align, "unexpected block alignment"),
        (bits == args.bits_per_sample, "unexpected bits per sample"),
        (data_tag == b"data", "missing data chunk"),
        (riff_size == len(data) - 8, "RIFF size does not match file size"),
        (data_size == len(data) - 44, "data size does not match file size"),
        (data_size >= args.min_data_bytes, "not enough PCM data"),
    ]
    for ok, message in checks:
        if not ok:
            raise ValueError(message)

    print(
        f"verified {path}: {data_size} PCM bytes, "
        f"{sample_rate} Hz, {channels} channel(s), {bits} bits"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
