"""Tests for the WarpViz wire protocol (framing + envelopes).

Run:  python -m unittest test_protocol  (or Tests/Python/run_all.py)
"""

import os
import sys
import unittest

_SCRIPTS = os.path.abspath(os.path.join(
    os.path.dirname(__file__), "..", "..", "UnrealGymProject", "Source", "Programs",
    "AnimForgeMayaWarpViz", "Scripts"))
sys.path.insert(0, _SCRIPTS)

import warpviz_protocol as proto


def _sample_request(**overrides):
    request = proto.EvaluateRequest(
        request_id="a" * 32,
        character_id="AF_Mannequin",
        clip_name="MM_Vault_Low",
        range=proto.TimeRange(10.0, 50.0, 30.0),
        warp_window=proto.TimeRange(20.0, 45.0, 30.0),
        method="SkewWarp",
        target=proto.WarpTarget("warpTarget_loc", (120.0, 0.0, 340.0), (0.0, 0.7071, 0.0, 0.7071)),
        warp_rotation=True,
        warp_translation=True,
        ghost_interval_frames=5.0,
        maya_root_samples=[
            proto.TrajectorySample(0.0, (0.0, 0.0, 0.0)),
            proto.TrajectorySample(0.5, (0.0, 0.0, 50.0), (0.0, 0.1, 0.0, 0.99)),
        ],
    )
    for key, value in overrides.items():
        setattr(request, key, value)
    return request


class FramingTests(unittest.TestCase):

    def test_frame_roundtrip(self):
        payload = b'{"hello":"world"}'
        decoder = proto.FrameDecoder()
        decoder.feed(proto.encode_frame(payload))
        self.assertEqual(decoder.next(), payload)
        self.assertIsNone(decoder.next())
        self.assertEqual(decoder.dropped_bytes, 0)

    def test_fragmented_delivery_byte_by_byte(self):
        payload = b"x" * 300
        frame = proto.encode_frame(payload)
        decoder = proto.FrameDecoder()
        for i in range(len(frame)):
            decoder.feed(frame[i:i + 1])
            if i < len(frame) - 1:
                self.assertIsNone(decoder.next())
        self.assertEqual(decoder.next(), payload)

    def test_multiple_frames_in_one_feed(self):
        first, second = b"first", b"second-payload"
        decoder = proto.FrameDecoder()
        decoder.feed(proto.encode_frame(first) + proto.encode_frame(second))
        self.assertEqual(decoder.next(), first)
        self.assertEqual(decoder.next(), second)
        self.assertIsNone(decoder.next())

    def test_resync_after_garbage(self):
        payload = b"clean"
        decoder = proto.FrameDecoder()
        decoder.feed(b"\x00\xffGARBAGE" + proto.encode_frame(payload))
        self.assertEqual(decoder.next(), payload)
        self.assertEqual(decoder.dropped_bytes, 9)

    def test_corrupt_length_skips_to_next_frame(self):
        # A frame whose length field is absurd must be skipped, not honored.
        bogus = proto.FRAME_MAGIC + (0xFFFFFFFF).to_bytes(4, "little") + b"junk"
        decoder = proto.FrameDecoder()
        decoder.feed(bogus + proto.encode_frame(b"good"))
        self.assertEqual(decoder.next(), b"good")
        self.assertGreater(decoder.dropped_bytes, 0)

    def test_empty_payload_frame(self):
        decoder = proto.FrameDecoder()
        decoder.feed(proto.encode_frame(b""))
        self.assertEqual(decoder.next(), b"")

    def test_partial_magic_at_tail_is_kept(self):
        decoder = proto.FrameDecoder()
        frame = proto.encode_frame(b"late")
        decoder.feed(frame[:2])  # "AF" only
        self.assertIsNone(decoder.next())
        decoder.feed(frame[2:])
        self.assertEqual(decoder.next(), b"late")
        self.assertEqual(decoder.dropped_bytes, 0)


class EnvelopeTests(unittest.TestCase):

    def test_handshake_roundtrip(self):
        payload = proto.encode_handshake("Maya 2025", "AF_Mannequin")
        decoded = proto.decode_handshake(payload)
        self.assertEqual(decoded["client_name"], "Maya 2025")
        self.assertEqual(decoded["character_id"], "AF_Mannequin")

    def test_handshake_ack_roundtrip(self):
        payload = proto.encode_handshake_ack("UE 5.4", ["ClipA", "ClipB"])
        decoded = proto.decode_handshake_ack(payload)
        self.assertEqual(decoded["server_name"], "UE 5.4")
        self.assertEqual(decoded["known_clips"], ["ClipA", "ClipB"])

    def test_evaluate_request_roundtrip(self):
        request = _sample_request()
        decoded = proto.decode_evaluate_request(proto.encode_evaluate_request(request))
        self.assertEqual(decoded.request_id, request.request_id)
        self.assertEqual(decoded.clip_name, "MM_Vault_Low")
        self.assertEqual(decoded.method, "SkewWarp")
        self.assertEqual(decoded.range.start_frame, 10.0)
        self.assertEqual(decoded.warp_window.end_frame, 45.0)
        self.assertEqual(decoded.target.name, "warpTarget_loc")
        self.assertEqual(decoded.target.translation, (120.0, 0.0, 340.0))
        self.assertEqual(len(decoded.maya_root_samples), 2)
        self.assertEqual(decoded.maya_root_samples[1].translation, (0.0, 0.0, 50.0))

    def test_evaluate_request_missing_window_defaults_to_range(self):
        request = _sample_request()
        import json
        doc = json.loads(proto.encode_evaluate_request(request))
        del doc["payload"]["warpWindow"]
        decoded = proto.decode_evaluate_request(json.dumps(doc).encode("utf-8"))
        self.assertEqual(decoded.warp_window.start_frame, decoded.range.start_frame)
        self.assertEqual(decoded.warp_window.end_frame, decoded.range.end_frame)

    def test_evaluate_request_rejects_unknown_method(self):
        request = _sample_request(method="TeleportWarp")
        payload = proto.encode_evaluate_request(request)
        with self.assertRaises(proto.ProtocolError):
            proto.decode_evaluate_request(payload)

    def test_evaluate_request_rejects_missing_request_id(self):
        request = _sample_request(request_id="")
        payload = proto.encode_evaluate_request(request)
        with self.assertRaises(proto.ProtocolError):
            proto.decode_evaluate_request(payload)

    def test_version_mismatch_rejected(self):
        import json
        doc = json.loads(proto.encode_handshake("Maya", "X"))
        doc["protocolVersion"] = 99
        with self.assertRaises(proto.ProtocolError):
            proto.decode_handshake(json.dumps(doc).encode("utf-8"))

    def test_wrong_type_rejected(self):
        payload = proto.encode_handshake("Maya", "X")
        with self.assertRaises(proto.ProtocolError):
            proto.decode_evaluate_request(payload)

    def test_evaluate_result_roundtrip(self):
        result = proto.EvaluateResult(
            request_id="b" * 32,
            success=True,
            warped_trajectory=[proto.TrajectorySample(0.1, (1.0, 2.0, 3.0))],
            original_trajectory=[proto.TrajectorySample(0.1, (1.0, 2.0, 2.5))],
            ghost_poses=[proto.GhostPose(0.1, [
                {"name": "root", "pos": [1.0, 2.0, 3.0], "rot": [0.0, 0.0, 0.0, 1.0]}])],
            warnings=["root drift 0.3cm"],
            evaluation_ms=12.5,
        )
        decoded = proto.decode_evaluate_result(proto.encode_evaluate_result(result))
        self.assertTrue(decoded.success)
        self.assertEqual(decoded.warped_trajectory[0].translation, (1.0, 2.0, 3.0))
        self.assertEqual(decoded.original_trajectory[0].translation, (1.0, 2.0, 2.5))
        self.assertEqual(decoded.ghost_poses[0].joints[0]["name"], "root")
        self.assertEqual(decoded.warnings, ["root drift 0.3cm"])
        self.assertEqual(decoded.evaluation_ms, 12.5)

    def test_error_roundtrip(self):
        decoded = proto.decode_error(proto.encode_error("boom", request_id="c" * 32))
        self.assertEqual(decoded["message"], "boom")
        self.assertEqual(decoded["request_id"], "c" * 32)

    def test_peek_envelope(self):
        msg_type, version = proto.peek_envelope(proto.encode_handshake("Maya", "X"))
        self.assertEqual(msg_type, proto.MSG_HANDSHAKE)
        self.assertEqual(version, proto.PROTOCOL_VERSION)

    def test_unicode_survives_roundtrip(self):
        payload = proto.encode_handshake("Maya éü中文", "charß")
        decoded = proto.decode_handshake(payload)
        self.assertEqual(decoded["client_name"], "Maya éü中文")
        self.assertEqual(decoded["character_id"], "charß")


if __name__ == "__main__":
    unittest.main()
