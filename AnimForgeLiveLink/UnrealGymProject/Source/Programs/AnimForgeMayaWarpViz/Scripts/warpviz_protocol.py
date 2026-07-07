"""AnimForge WarpViz wire protocol - Python mirror.

This module is a 1:1 mirror of Shared/AnimForgeWarpVizShared/WarpVizProtocol.h
(.cpp). It is used by:

  * the automated test suite (AnimForgeLiveLink/Tests/Python),
  * mock_unreal_server.py - a stand-in for the Unreal gym so the Maya side of
    the pipeline can be exercised without an engine session,
  * optional Maya-side Python tooling that wants to talk to the gym directly.

Frame layout (little-endian):
    [4 bytes magic b"AFWV"] [4 bytes payload length] [payload: UTF-8 JSON]

Envelope:
    {"type": "<MessageType>", "protocolVersion": 1, "payload": {...}}

Keep this file dependency-free (stdlib only) so it runs inside mayapy,
a plain CPython, and CI without any pip installs.
"""

from __future__ import annotations

import json
import struct
import uuid
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

PROTOCOL_VERSION = 1
DEFAULT_PORT = 46464
FRAME_MAGIC = b"AFWV"
MAX_PAYLOAD_BYTES = 256 * 1024 * 1024

WARP_METHODS = ("SkewWarp", "SimpleWarp", "Scale")

MSG_HANDSHAKE = "Handshake"
MSG_HANDSHAKE_ACK = "HandshakeAck"
MSG_EVALUATE_REQUEST = "EvaluateRequest"
MSG_EVALUATE_PROGRESS = "EvaluateProgress"
MSG_EVALUATE_RESULT = "EvaluateResult"
MSG_ERROR = "Error"


class ProtocolError(ValueError):
    """Raised when a payload fails validation."""


# ---------------------------------------------------------------------------
# Framing
# ---------------------------------------------------------------------------

def encode_frame(payload: bytes) -> bytes:
    """Wraps a UTF-8 JSON payload in [magic][length][payload]."""
    return FRAME_MAGIC + struct.pack("<I", len(payload)) + payload


class FrameDecoder:
    """Incremental frame decoder mirroring the C++ FrameDecoder.

    Feed raw socket bytes in arbitrary fragmentation; iterate next() until it
    returns None. Resynchronizes on the magic when the stream is corrupted.
    """

    def __init__(self) -> None:
        self._buffer = bytearray()
        self.dropped_bytes = 0

    def feed(self, data: bytes) -> None:
        self._buffer.extend(data)

    def _resync(self) -> None:
        drop = 0
        n = len(self._buffer)
        while drop < n:
            compare = min(4, n - drop)
            if self._buffer[drop:drop + compare] == FRAME_MAGIC[:compare]:
                break
            drop += 1
        if drop:
            del self._buffer[:drop]
            self.dropped_bytes += drop

    def next(self) -> Optional[bytes]:
        """Pops the next complete payload, or None if no full frame is buffered."""
        while True:
            self._resync()
            if len(self._buffer) < 8:
                return None
            (length,) = struct.unpack_from("<I", self._buffer, 4)
            if length > MAX_PAYLOAD_BYTES:
                # Corrupt length: skip this magic and resync further on.
                del self._buffer[:4]
                self.dropped_bytes += 4
                continue
            if len(self._buffer) < 8 + length:
                return None
            payload = bytes(self._buffer[8:8 + length])
            del self._buffer[:8 + length]
            return payload


# ---------------------------------------------------------------------------
# Message dataclasses
# ---------------------------------------------------------------------------

Vec3 = Tuple[float, float, float]
Quat = Tuple[float, float, float, float]  # (x, y, z, w)

QUAT_IDENTITY: Quat = (0.0, 0.0, 0.0, 1.0)


@dataclass
class TimeRange:
    start_frame: float = 0.0
    end_frame: float = 0.0
    fps: float = 30.0

    def duration_seconds(self) -> float:
        return (self.end_frame - self.start_frame) / self.fps if self.fps > 0 else 0.0

    def to_json(self) -> Dict:
        return {"startFrame": self.start_frame, "endFrame": self.end_frame, "fps": self.fps}

    @classmethod
    def from_json(cls, data: Dict) -> "TimeRange":
        return cls(
            start_frame=float(data.get("startFrame", 0.0)),
            end_frame=float(data.get("endFrame", 0.0)),
            fps=float(data.get("fps", 30.0)),
        )


@dataclass
class TrajectorySample:
    time_seconds: float
    translation: Vec3
    rotation: Quat = QUAT_IDENTITY

    def to_json(self) -> Dict:
        return {"t": self.time_seconds, "pos": list(self.translation), "rot": list(self.rotation)}

    @classmethod
    def from_json(cls, data: Dict) -> "TrajectorySample":
        pos = data.get("pos")
        rot = data.get("rot")
        if not isinstance(pos, list) or len(pos) != 3:
            raise ProtocolError("trajectory sample 'pos' must be a 3-array")
        if not isinstance(rot, list) or len(rot) != 4:
            raise ProtocolError("trajectory sample 'rot' must be a 4-array")
        return cls(float(data.get("t", 0.0)), tuple(map(float, pos)), tuple(map(float, rot)))


@dataclass
class WarpTarget:
    name: str = ""
    translation: Vec3 = (0.0, 0.0, 0.0)
    rotation: Quat = QUAT_IDENTITY

    def to_json(self) -> Dict:
        return {"name": self.name, "pos": list(self.translation), "rot": list(self.rotation)}

    @classmethod
    def from_json(cls, data: Dict) -> "WarpTarget":
        pos = data.get("pos")
        rot = data.get("rot")
        if not isinstance(pos, list) or len(pos) != 3 or not isinstance(rot, list) or len(rot) != 4:
            raise ProtocolError("warp target transform malformed")
        return cls(str(data.get("name", "")), tuple(map(float, pos)), tuple(map(float, rot)))


@dataclass
class GhostPose:
    time_seconds: float
    joints: List[Dict] = field(default_factory=list)  # {"name", "pos", "rot"}

    def to_json(self) -> Dict:
        return {"t": self.time_seconds, "joints": self.joints}

    @classmethod
    def from_json(cls, data: Dict) -> "GhostPose":
        return cls(float(data.get("t", 0.0)), list(data.get("joints", [])))


@dataclass
class EvaluateRequest:
    request_id: str
    character_id: str
    clip_name: str
    range: TimeRange
    warp_window: TimeRange
    method: str = "SkewWarp"
    target: WarpTarget = field(default_factory=WarpTarget)
    warp_rotation: bool = True
    warp_translation: bool = True
    ghost_interval_frames: float = 5.0
    maya_root_samples: List[TrajectorySample] = field(default_factory=list)

    @staticmethod
    def new_request_id() -> str:
        return uuid.uuid4().hex


@dataclass
class EvaluateResult:
    request_id: str
    success: bool = False
    error_message: str = ""
    warped_trajectory: List[TrajectorySample] = field(default_factory=list)
    original_trajectory: List[TrajectorySample] = field(default_factory=list)
    ghost_poses: List[GhostPose] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)
    evaluation_ms: float = 0.0


# ---------------------------------------------------------------------------
# Envelope encode / decode
# ---------------------------------------------------------------------------

def _envelope(msg_type: str, payload: Dict) -> bytes:
    doc = {"type": msg_type, "protocolVersion": PROTOCOL_VERSION, "payload": payload}
    return json.dumps(doc, separators=(",", ":")).encode("utf-8")


def peek_envelope(payload: bytes) -> Tuple[str, int]:
    """Returns (type, protocolVersion) without validating the payload body."""
    doc = json.loads(payload.decode("utf-8"))
    return str(doc.get("type", "Unknown")), int(doc.get("protocolVersion", 0))


def _open_envelope(payload: bytes, expected_type: str) -> Dict:
    try:
        doc = json.loads(payload.decode("utf-8"))
    except (ValueError, UnicodeDecodeError) as exc:
        raise ProtocolError("payload is not valid JSON: %s" % exc)
    version = int(doc.get("protocolVersion", 0))
    if version != PROTOCOL_VERSION:
        raise ProtocolError(
            "protocol version mismatch: got %d, expected %d" % (version, PROTOCOL_VERSION))
    if doc.get("type") != expected_type:
        raise ProtocolError(
            "unexpected message type: got %r, expected %r" % (doc.get("type"), expected_type))
    body = doc.get("payload")
    if not isinstance(body, dict):
        raise ProtocolError("missing 'payload' object")
    return body


# --- Handshake --------------------------------------------------------------

def encode_handshake(client_name: str, character_id: str) -> bytes:
    return _envelope(MSG_HANDSHAKE, {"clientName": client_name, "characterId": character_id})


def decode_handshake(payload: bytes) -> Dict:
    body = _open_envelope(payload, MSG_HANDSHAKE)
    return {"client_name": body.get("clientName", ""), "character_id": body.get("characterId", "")}


def encode_handshake_ack(server_name: str, known_clips: List[str]) -> bytes:
    return _envelope(MSG_HANDSHAKE_ACK, {"serverName": server_name, "knownClips": list(known_clips)})


def decode_handshake_ack(payload: bytes) -> Dict:
    body = _open_envelope(payload, MSG_HANDSHAKE_ACK)
    return {
        "server_name": body.get("serverName", ""),
        "known_clips": [c for c in body.get("knownClips", []) if isinstance(c, str)],
    }


# --- EvaluateRequest ---------------------------------------------------------

def encode_evaluate_request(req: EvaluateRequest) -> bytes:
    return _envelope(MSG_EVALUATE_REQUEST, {
        "requestId": req.request_id,
        "characterId": req.character_id,
        "clipName": req.clip_name,
        "range": req.range.to_json(),
        "warpWindow": req.warp_window.to_json(),
        "method": req.method,
        "target": req.target.to_json(),
        "warpRotation": req.warp_rotation,
        "warpTranslation": req.warp_translation,
        "ghostIntervalFrames": req.ghost_interval_frames,
        "mayaRootSamples": [s.to_json() for s in req.maya_root_samples],
    })


def decode_evaluate_request(payload: bytes) -> EvaluateRequest:
    body = _open_envelope(payload, MSG_EVALUATE_REQUEST)

    request_id = str(body.get("requestId", ""))
    if not request_id:
        raise ProtocolError("EvaluateRequest missing 'requestId'")
    method = str(body.get("method", ""))
    if method not in WARP_METHODS:
        raise ProtocolError("EvaluateRequest has unknown warp method %r" % method)
    if not isinstance(body.get("range"), dict):
        raise ProtocolError("EvaluateRequest missing 'range'")
    if not isinstance(body.get("target"), dict):
        raise ProtocolError("EvaluateRequest missing 'target'")

    rng = TimeRange.from_json(body["range"])
    window = TimeRange.from_json(body["warpWindow"]) if isinstance(body.get("warpWindow"), dict) else rng

    return EvaluateRequest(
        request_id=request_id,
        character_id=str(body.get("characterId", "")),
        clip_name=str(body.get("clipName", "")),
        range=rng,
        warp_window=window,
        method=method,
        target=WarpTarget.from_json(body["target"]),
        warp_rotation=bool(body.get("warpRotation", True)),
        warp_translation=bool(body.get("warpTranslation", True)),
        ghost_interval_frames=float(body.get("ghostIntervalFrames", 5.0)),
        maya_root_samples=[TrajectorySample.from_json(s) for s in body.get("mayaRootSamples", [])],
    )


# --- EvaluateProgress --------------------------------------------------------

def encode_evaluate_progress(request_id: str, progress: float, stage: str) -> bytes:
    return _envelope(MSG_EVALUATE_PROGRESS,
                     {"requestId": request_id, "progress": progress, "stage": stage})


def decode_evaluate_progress(payload: bytes) -> Dict:
    body = _open_envelope(payload, MSG_EVALUATE_PROGRESS)
    return {
        "request_id": body.get("requestId", ""),
        "progress": float(body.get("progress", 0.0)),
        "stage": body.get("stage", ""),
    }


# --- EvaluateResult ----------------------------------------------------------

def encode_evaluate_result(res: EvaluateResult) -> bytes:
    return _envelope(MSG_EVALUATE_RESULT, {
        "requestId": res.request_id,
        "success": res.success,
        "errorMessage": res.error_message,
        "warpedTrajectory": [s.to_json() for s in res.warped_trajectory],
        "originalTrajectory": [s.to_json() for s in res.original_trajectory],
        "ghostPoses": [g.to_json() for g in res.ghost_poses],
        "warnings": list(res.warnings),
        "evaluationMs": res.evaluation_ms,
    })


def decode_evaluate_result(payload: bytes) -> EvaluateResult:
    body = _open_envelope(payload, MSG_EVALUATE_RESULT)
    return EvaluateResult(
        request_id=str(body.get("requestId", "")),
        success=bool(body.get("success", False)),
        error_message=str(body.get("errorMessage", "")),
        warped_trajectory=[TrajectorySample.from_json(s) for s in body.get("warpedTrajectory", [])],
        original_trajectory=[TrajectorySample.from_json(s) for s in body.get("originalTrajectory", [])],
        ghost_poses=[GhostPose.from_json(g) for g in body.get("ghostPoses", [])],
        warnings=[w for w in body.get("warnings", []) if isinstance(w, str)],
        evaluation_ms=float(body.get("evaluationMs", 0.0)),
    )


# --- Error -------------------------------------------------------------------

def encode_error(message: str, request_id: str = "") -> bytes:
    return _envelope(MSG_ERROR, {"requestId": request_id, "message": message})


def decode_error(payload: bytes) -> Dict:
    body = _open_envelope(payload, MSG_ERROR)
    return {"request_id": body.get("requestId", ""), "message": body.get("message", "")}
