import struct
import unittest

from qwen_tts_bridge_worker.protocol import (
    MIN_HEADER_SIZE,
    PROTOCOL_VERSION,
    Frame,
    FrameHeader,
    FrameParser,
    FrameType,
    ParseResult,
    ParseStatus,
    ProtocolError,
    encode_frame,
    encode_frame_with_header,
)


def _patch_u16(frame: bytearray, offset: int, value: int) -> None:
    frame[offset : offset + 2] = struct.pack("<H", value)


def _patch_u32(frame: bytearray, offset: int, value: int) -> None:
    frame[offset : offset + 4] = struct.pack("<I", value)


def _require_frame(result: ParseResult) -> Frame:
    assert result.frame is not None
    return result.frame


class ProtocolFrameTests(unittest.TestCase):
    def test_round_trip_control_frame(self) -> None:
        payload = b'{"message_type":"ping","sequence":1}'
        encoded = encode_frame(FrameType.CONTROL_JSON, 0, payload)

        parser = FrameParser()
        parser.append(encoded)
        result = parser.parse_next()

        self.assertEqual(ParseStatus.FRAME_READY, result.status)
        self.assertEqual(ProtocolError.NONE, result.error)
        self.assertIsNotNone(result.frame)
        frame = _require_frame(result)
        self.assertEqual(PROTOCOL_VERSION, frame.header.protocol_version)
        self.assertEqual(MIN_HEADER_SIZE, frame.header.header_size)
        self.assertEqual(FrameType.CONTROL_JSON, frame.header.frame_type)
        self.assertEqual(0, frame.header.flags)
        self.assertEqual(len(payload), frame.header.payload_size)
        self.assertEqual(0, frame.header.request_id)
        self.assertEqual(payload, frame.payload)
        self.assertEqual(0, parser.buffered_size)

    def test_fragmented_frame(self) -> None:
        payload = b'{"message_type":"cancel"}'
        encoded = encode_frame(FrameType.CONTROL_JSON, 42, payload)

        parser = FrameParser()
        parser.append(encoded[:7])

        result = parser.parse_next()
        self.assertEqual(ParseStatus.NEED_MORE_DATA, result.status)

        parser.append(encoded[7:])
        result = parser.parse_next()

        self.assertEqual(ParseStatus.FRAME_READY, result.status)
        self.assertIsNotNone(result.frame)
        frame = _require_frame(result)
        self.assertEqual(42, frame.header.request_id)
        self.assertEqual(payload, frame.payload)

    def test_multiple_frames_in_one_read(self) -> None:
        first_payload = b'{"message_type":"ping","sequence":1}'
        second_payload = b'{"message_type":"ping","sequence":2}'
        encoded = (
            encode_frame(FrameType.CONTROL_JSON, 0, first_payload)
            + encode_frame(FrameType.CONTROL_JSON, 0, second_payload)
        )

        parser = FrameParser()
        parser.append(encoded)

        first = parser.parse_next()
        self.assertEqual(ParseStatus.FRAME_READY, first.status)
        self.assertIsNotNone(first.frame)
        first_frame = _require_frame(first)
        self.assertEqual(first_payload, first_frame.payload)

        second = parser.parse_next()
        self.assertEqual(ParseStatus.FRAME_READY, second.status)
        self.assertIsNotNone(second.frame)
        second_frame = _require_frame(second)
        self.assertEqual(second_payload, second_frame.payload)

        empty = parser.parse_next()
        self.assertEqual(ParseStatus.NEED_MORE_DATA, empty.status)

    def test_extended_header_is_skipped(self) -> None:
        payload = b'{"message_type":"pong","sequence":1}'
        encoded = bytearray(encode_frame(FrameType.CONTROL_JSON, 0, payload))
        _patch_u16(encoded, 6, 28)
        encoded[24:24] = b"\xaa\xbb\xcc\xdd"

        parser = FrameParser()
        parser.append(encoded)

        result = parser.parse_next()

        self.assertEqual(ParseStatus.FRAME_READY, result.status)
        self.assertIsNotNone(result.frame)
        frame = _require_frame(result)
        self.assertEqual(28, frame.header.header_size)
        self.assertEqual(payload, frame.payload)

    def test_reject_bad_magic(self) -> None:
        encoded = bytearray(encode_frame(FrameType.CONTROL_JSON, 0, b"{}"))
        encoded[0] = ord("X")

        parser = FrameParser()
        parser.append(encoded)
        result = parser.parse_next()

        self.assertEqual(ParseStatus.FATAL_ERROR, result.status)
        self.assertEqual(ProtocolError.INVALID_MAGIC, result.error)

    def test_reject_unsupported_version(self) -> None:
        encoded = bytearray(encode_frame(FrameType.CONTROL_JSON, 0, b"{}"))
        _patch_u16(encoded, 4, 2)

        parser = FrameParser()
        parser.append(encoded)
        result = parser.parse_next()

        self.assertEqual(ParseStatus.FATAL_ERROR, result.status)
        self.assertEqual(ProtocolError.UNSUPPORTED_PROTOCOL_VERSION, result.error)

    def test_reject_header_too_small(self) -> None:
        encoded = bytearray(encode_frame(FrameType.CONTROL_JSON, 0, b"{}"))
        _patch_u16(encoded, 6, 23)

        parser = FrameParser()
        parser.append(encoded)
        result = parser.parse_next()

        self.assertEqual(ParseStatus.FATAL_ERROR, result.status)
        self.assertEqual(ProtocolError.HEADER_TOO_SMALL, result.error)

    def test_reject_unknown_frame_type(self) -> None:
        encoded = bytearray(encode_frame(FrameType.CONTROL_JSON, 0, b"{}"))
        _patch_u16(encoded, 8, 99)

        parser = FrameParser()
        parser.append(encoded)
        result = parser.parse_next()

        self.assertEqual(ParseStatus.FATAL_ERROR, result.status)
        self.assertEqual(ProtocolError.UNKNOWN_FRAME_TYPE, result.error)

    def test_reject_non_zero_flags(self) -> None:
        encoded = bytearray(encode_frame(FrameType.CONTROL_JSON, 0, b"{}"))
        _patch_u16(encoded, 10, 1)

        parser = FrameParser()
        parser.append(encoded)
        result = parser.parse_next()

        self.assertEqual(ParseStatus.FATAL_ERROR, result.status)
        self.assertEqual(ProtocolError.UNSUPPORTED_FLAGS, result.error)

    def test_reject_oversized_control_payload_before_allocation(self) -> None:
        encoded = bytearray(encode_frame(FrameType.CONTROL_JSON, 0, b"{}"))
        _patch_u32(encoded, 12, 1024 * 1024 + 1)

        parser = FrameParser()
        parser.append(encoded[:MIN_HEADER_SIZE])
        result = parser.parse_next()

        self.assertEqual(ParseStatus.FATAL_ERROR, result.status)
        self.assertEqual(ProtocolError.PAYLOAD_TOO_LARGE, result.error)

    def test_fatal_parser_stays_fatal_until_clear(self) -> None:
        bad = bytearray(encode_frame(FrameType.CONTROL_JSON, 0, b"{}"))
        bad[0] = ord("X")
        good = encode_frame(FrameType.CONTROL_JSON, 0, b"{}")

        parser = FrameParser()
        parser.append(bad)

        result = parser.parse_next()
        self.assertEqual(ParseStatus.FATAL_ERROR, result.status)
        self.assertEqual(ProtocolError.INVALID_MAGIC, result.error)

        parser.append(good)
        result = parser.parse_next()
        self.assertEqual(ParseStatus.FATAL_ERROR, result.status)
        self.assertEqual(ProtocolError.INVALID_MAGIC, result.error)

        parser.clear()
        parser.append(good)
        result = parser.parse_next()
        self.assertEqual(ParseStatus.FRAME_READY, result.status)

    def test_encode_explicit_header_rejects_payload_size_mismatch(self) -> None:
        header = FrameHeader(payload_size=0)

        with self.assertRaises(ValueError):
            encode_frame_with_header(header, b"{}")


if __name__ == "__main__":
    unittest.main()
