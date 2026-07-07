"""Integration test: full protocol round trip against the mock gym server.

Spins mock_unreal_server.py up on an ephemeral port in a thread, then drives
the exact message sequence the Maya plugin sends:

    Handshake -> HandshakeAck -> EvaluateRequest -> (Progress) -> EvaluateResult

and verifies the warped trajectory endpoint lands on the warp target.
"""

import os
import socket
import sys
import threading
import unittest

_SCRIPTS = os.path.abspath(os.path.join(
    os.path.dirname(__file__), "..", "..", "UnrealGymProject", "Source", "Programs",
    "AnimForgeMayaWarpViz", "Scripts"))
sys.path.insert(0, _SCRIPTS)

import mock_unreal_server as mock_server
import warpviz_protocol as proto


class MockServerRoundTripTests(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        ready = threading.Event()
        cls._thread = threading.Thread(
            target=mock_server.serve, kwargs={"port": 0, "ready_event": ready}, daemon=True)
        cls._thread.start()
        if not ready.wait(timeout=10.0):
            raise RuntimeError("mock server failed to start")
        cls._port = ready.port

    def _connect(self):
        connection = socket.create_connection(("127.0.0.1", self._port), timeout=10.0)
        connection.settimeout(10.0)
        return connection

    def _receive_message(self, connection, decoder):
        while True:
            payload = decoder.next()
            if payload is not None:
                return payload
            data = connection.recv(65536)
            if not data:
                raise ConnectionError("server closed the connection")
            decoder.feed(data)

    def test_handshake(self):
        connection = self._connect()
        try:
            decoder = proto.FrameDecoder()
            connection.sendall(proto.encode_frame(
                proto.encode_handshake("test-client", "AF_Mannequin")))
            ack = proto.decode_handshake_ack(self._receive_message(connection, decoder))
            self.assertIn("MockGym", ack["server_name"])
            self.assertIn("MM_Vault_Low", ack["known_clips"])
        finally:
            connection.close()

    def test_evaluate_roundtrip_endpoint_hits_target(self):
        fps = 30.0
        frames = 31
        samples = [
            proto.TrajectorySample(i / fps, (0.0, 0.0, 10.0 * i)) for i in range(frames)
        ]
        target = (120.0, 0.0, 250.0)
        request = proto.EvaluateRequest(
            request_id=proto.EvaluateRequest.new_request_id(),
            character_id="AF_Mannequin",
            clip_name="MM_Vault_Low",
            range=proto.TimeRange(0.0, float(frames - 1), fps),
            warp_window=proto.TimeRange(0.0, float(frames - 1), fps),
            method="SkewWarp",
            target=proto.WarpTarget("loc", target),
            warp_rotation=False,
            ghost_interval_frames=10.0,
            maya_root_samples=samples,
        )

        connection = self._connect()
        try:
            decoder = proto.FrameDecoder()
            connection.sendall(proto.encode_frame(proto.encode_evaluate_request(request)))

            # Server sends Progress first, then the Result.
            result = None
            saw_progress = False
            for _ in range(10):
                payload = self._receive_message(connection, decoder)
                msg_type, _ = proto.peek_envelope(payload)
                if msg_type == proto.MSG_EVALUATE_PROGRESS:
                    saw_progress = True
                    continue
                if msg_type == proto.MSG_EVALUATE_RESULT:
                    result = proto.decode_evaluate_result(payload)
                    break
                self.fail("unexpected message type %r" % msg_type)

            self.assertTrue(saw_progress)
            self.assertIsNotNone(result)
            self.assertTrue(result.success, result.error_message)
            self.assertEqual(result.request_id, request.request_id)
            self.assertEqual(len(result.warped_trajectory), frames)
            self.assertEqual(len(result.original_trajectory), frames)

            end = result.warped_trajectory[-1].translation
            for got, expected in zip(end, target):
                self.assertAlmostEqual(got, expected, places=6)

            # Ghosts sampled every 10 frames over 31 frames -> 4 poses (0,10,20,30).
            self.assertEqual(len(result.ghost_poses), 4)
            self.assertEqual(result.ghost_poses[0].joints[0]["name"], "root")
        finally:
            connection.close()

    def test_evaluate_without_samples_fails_gracefully(self):
        request = proto.EvaluateRequest(
            request_id=proto.EvaluateRequest.new_request_id(),
            character_id="AF_Mannequin",
            clip_name="MM_Vault_Low",
            range=proto.TimeRange(0.0, 30.0, 30.0),
            warp_window=proto.TimeRange(0.0, 30.0, 30.0),
            target=proto.WarpTarget("loc", (1.0, 2.0, 3.0)),
            maya_root_samples=[],
        )
        connection = self._connect()
        try:
            decoder = proto.FrameDecoder()
            connection.sendall(proto.encode_frame(proto.encode_evaluate_request(request)))
            result = None
            for _ in range(10):
                payload = self._receive_message(connection, decoder)
                msg_type, _ = proto.peek_envelope(payload)
                if msg_type == proto.MSG_EVALUATE_RESULT:
                    result = proto.decode_evaluate_result(payload)
                    break
            self.assertIsNotNone(result)
            self.assertFalse(result.success)
            self.assertIn("mayaRootSamples", result.error_message)
        finally:
            connection.close()

    def test_malformed_payload_returns_error(self):
        connection = self._connect()
        try:
            decoder = proto.FrameDecoder()
            connection.sendall(proto.encode_frame(b"this is not json"))
            error = proto.decode_error(self._receive_message(connection, decoder))
            self.assertIn("Malformed", error["message"])
        finally:
            connection.close()


if __name__ == "__main__":
    unittest.main()
