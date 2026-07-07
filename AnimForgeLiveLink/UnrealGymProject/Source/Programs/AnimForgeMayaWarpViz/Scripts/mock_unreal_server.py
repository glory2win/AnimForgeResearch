"""Mock AnimForgeUnrealWarpViz gym server.

A stdlib-only stand-in for the Unreal side of the bridge. It speaks the full
WarpViz protocol and evaluates warps with warpviz_skewwarp (the 1:1 Python
mirror of the shared C++ math), so the Maya plugin / UI loop can be exercised
end-to-end without an engine session:

    Maya (.mll or Python)  <-- TCP -->  mock_unreal_server.py

Instead of extracting root motion from a real montage, the mock synthesizes
the "engine" trajectory from the Maya root samples embedded in the request
(exactly what a matching gym asset would produce). Ghost poses contain the
root joint only.

Usage:
    python mock_unreal_server.py [--port 46464] [--once]

    --once   handle a single connection then exit (used by the test suite)
"""

from __future__ import annotations

import argparse
import socket
import sys
import threading
import time

import warpviz_protocol as proto
import warpviz_skewwarp as skew

SERVER_NAME = "MockGym/1.0 (mock_unreal_server.py)"
KNOWN_CLIPS = ["MM_Jump_Forward", "MM_Vault_Low", "MM_Climb_Ledge"]


def evaluate_request(request: proto.EvaluateRequest) -> proto.EvaluateResult:
    """Runs the shared skew-warp math over the request's root samples."""
    started = time.perf_counter()
    result = proto.EvaluateResult(request_id=request.request_id)

    if not request.maya_root_samples:
        result.error_message = (
            "Mock server needs 'mayaRootSamples' in the request; the real gym "
            "would extract root motion from clip %r instead." % request.clip_name)
        return result

    fps = request.range.fps
    params = skew.WarpParams(
        window_start_seconds=(request.warp_window.start_frame - request.range.start_frame) / fps,
        window_end_seconds=(request.warp_window.end_frame - request.range.start_frame) / fps,
        target_translation=request.target.translation,
        target_rotation=request.target.rotation,
        method=request.method,
        warp_rotation=request.warp_rotation,
        warp_translation=request.warp_translation,
    )

    original = request.maya_root_samples
    warped = skew.warp_trajectory(original, params)

    result.success = True
    result.original_trajectory = original
    result.warped_trajectory = warped

    if request.ghost_interval_frames > 0:
        interval_seconds = request.ghost_interval_frames / fps
        next_ghost_time = warped[0].time_seconds
        for sample in warped:
            if sample.time_seconds + 1e-9 >= next_ghost_time:
                result.ghost_poses.append(proto.GhostPose(
                    time_seconds=sample.time_seconds,
                    joints=[{
                        "name": "root",
                        "pos": list(sample.translation),
                        "rot": list(sample.rotation),
                    }],
                ))
                next_ghost_time += interval_seconds

    result.warnings.append("Mock evaluation: trajectory synthesized from Maya root samples.")
    result.evaluation_ms = (time.perf_counter() - started) * 1000.0
    return result


def _send(connection: socket.socket, payload: bytes) -> None:
    connection.sendall(proto.encode_frame(payload))


def handle_connection(connection: socket.socket, address) -> None:
    decoder = proto.FrameDecoder()
    print("[mock-gym] connection from %s:%d" % address)
    try:
        while True:
            data = connection.recv(65536)
            if not data:
                break
            decoder.feed(data)
            while True:
                payload = decoder.next()
                if payload is None:
                    break
                _dispatch(connection, payload)
    except ConnectionError as exc:
        print("[mock-gym] connection dropped: %s" % exc)
    finally:
        connection.close()
        print("[mock-gym] connection closed")


def _dispatch(connection: socket.socket, payload: bytes) -> None:
    try:
        msg_type, version = proto.peek_envelope(payload)
    except ValueError as exc:
        _send(connection, proto.encode_error("Malformed payload: %s" % exc))
        return

    if version != proto.PROTOCOL_VERSION:
        _send(connection, proto.encode_error(
            "Protocol version mismatch: got %d, expected %d" % (version, proto.PROTOCOL_VERSION)))
        return

    if msg_type == proto.MSG_HANDSHAKE:
        handshake = proto.decode_handshake(payload)
        print("[mock-gym] handshake from %r (character %r)"
              % (handshake["client_name"], handshake["character_id"]))
        _send(connection, proto.encode_handshake_ack(SERVER_NAME, KNOWN_CLIPS))
    elif msg_type == proto.MSG_EVALUATE_REQUEST:
        try:
            request = proto.decode_evaluate_request(payload)
        except proto.ProtocolError as exc:
            _send(connection, proto.encode_error(str(exc)))
            return
        print("[mock-gym] evaluate %s: clip=%r method=%s frames=[%g, %g]"
              % (request.request_id[:8], request.clip_name, request.method,
                 request.range.start_frame, request.range.end_frame))
        _send(connection, proto.encode_evaluate_progress(request.request_id, 0.5, "warping"))
        result = evaluate_request(request)
        _send(connection, proto.encode_evaluate_result(result))
    else:
        _send(connection, proto.encode_error("Mock server cannot handle %r" % msg_type))


def serve(port: int, once: bool = False, ready_event: "threading.Event | None" = None) -> None:
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("127.0.0.1", port))
    listener.listen(4)
    actual_port = listener.getsockname()[1]
    print("[mock-gym] listening on 127.0.0.1:%d" % actual_port)
    if ready_event is not None:
        ready_event.port = actual_port  # type: ignore[attr-defined]
        ready_event.set()
    try:
        while True:
            connection, address = listener.accept()
            if once:
                handle_connection(connection, address)
                break
            thread = threading.Thread(target=handle_connection, args=(connection, address), daemon=True)
            thread.start()
    finally:
        listener.close()


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=proto.DEFAULT_PORT)
    parser.add_argument("--once", action="store_true",
                        help="handle a single connection then exit")
    args = parser.parse_args(argv)
    try:
        serve(args.port, once=args.once)
    except KeyboardInterrupt:
        print("[mock-gym] shutting down")
    return 0


if __name__ == "__main__":
    sys.exit(main())
