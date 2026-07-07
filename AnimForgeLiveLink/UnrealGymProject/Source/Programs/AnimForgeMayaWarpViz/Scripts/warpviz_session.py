"""UI-independent session logic for the AnimForge WarpViz Maya tool.

Everything the PySide dialog needs to validate and assemble an evaluation is
kept here, free of both Qt and maya.cmds imports, so it is unit-testable with
plain CPython (see Tests/Python/test_session.py). The dialog feeds scene data
in; this module hands a validated request description back.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import List, Optional, Tuple

from warpviz_protocol import (
    DEFAULT_PORT,
    WARP_METHODS,
    EvaluateRequest,
    TimeRange,
    TrajectorySample,
    WarpTarget,
)


@dataclass
class SessionSettings:
    """Everything the Evaluate button needs, as plain data."""
    host: str = "127.0.0.1"
    port: int = DEFAULT_PORT
    character_id: str = ""
    clip_name: str = ""
    start_frame: float = 0.0
    end_frame: float = 0.0
    fps: float = 30.0
    warp_method: str = "SkewWarp"
    warp_target_locator: str = ""
    warp_rotation: bool = True
    warp_translation: bool = True
    ghost_interval_frames: float = 5.0
    # Warp window; None means "use the full evaluation range".
    warp_window: Optional[Tuple[float, float]] = None


@dataclass
class ValidationResult:
    errors: List[str] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not self.errors


def validate_settings(settings: SessionSettings) -> ValidationResult:
    """Validates UI input before anything touches the network or the scene."""
    result = ValidationResult()

    if settings.end_frame <= settings.start_frame:
        result.errors.append(
            "End frame (%g) must be greater than start frame (%g)."
            % (settings.end_frame, settings.start_frame))
    if settings.fps <= 0.0:
        result.errors.append("FPS must be positive (got %g)." % settings.fps)
    if settings.warp_method not in WARP_METHODS:
        result.errors.append(
            "Unknown warp method %r. Available: %s"
            % (settings.warp_method, ", ".join(WARP_METHODS)))
    if not settings.warp_target_locator:
        result.errors.append("No warp target locator selected.")
    if not settings.clip_name:
        result.errors.append("Clip name is empty; it must match a clip registered in the gym.")
    if not settings.character_id:
        result.errors.append("Character ID is empty.")
    if not (0 < settings.port < 65536):
        result.errors.append("Port %d is out of range." % settings.port)
    if not settings.warp_rotation and not settings.warp_translation:
        result.errors.append("Nothing to warp: both rotation and translation warping are disabled.")

    if settings.warp_window is not None:
        w0, w1 = settings.warp_window
        if w1 <= w0:
            result.errors.append("Warp window end (%g) must be after its start (%g)." % (w1, w0))
        elif w0 < settings.start_frame or w1 > settings.end_frame:
            result.errors.append(
                "Warp window [%g, %g] must lie inside the evaluation range [%g, %g]."
                % (w0, w1, settings.start_frame, settings.end_frame))

    if settings.ghost_interval_frames <= 0:
        result.warnings.append("Ghost pose sampling disabled (interval <= 0).")
    elif settings.ghost_interval_frames > (settings.end_frame - settings.start_frame):
        result.warnings.append("Ghost interval is larger than the range; only endpoints will ghost.")

    frame_count = settings.end_frame - settings.start_frame
    if frame_count > 2000:
        result.warnings.append(
            "Evaluating %g frames; consider trimming the range for faster iterations."
            % frame_count)

    return result


def build_evaluate_request(settings: SessionSettings,
                           target: WarpTarget,
                           root_samples: List[TrajectorySample]) -> EvaluateRequest:
    """Assembles the wire request from validated settings plus scene data.

    `target` and `root_samples` are extracted from the Maya scene by the .mll
    (C++ path) or by the caller in Python-only workflows/tests.
    """
    rng = TimeRange(settings.start_frame, settings.end_frame, settings.fps)
    if settings.warp_window is not None:
        window = TimeRange(settings.warp_window[0], settings.warp_window[1], settings.fps)
    else:
        window = rng

    return EvaluateRequest(
        request_id=EvaluateRequest.new_request_id(),
        character_id=settings.character_id,
        clip_name=settings.clip_name,
        range=rng,
        warp_window=window,
        method=settings.warp_method,
        target=target,
        warp_rotation=settings.warp_rotation,
        warp_translation=settings.warp_translation,
        ghost_interval_frames=settings.ghost_interval_frames,
        maya_root_samples=root_samples,
    )


def build_evaluate_command(settings: SessionSettings) -> str:
    """Builds the MEL invocation of the .mll command for the C++ path.

    The PySide UI shells the actual work out to the compiled plugin; keeping
    the flag assembly here means the exact command string is unit-testable.
    """
    parts = ["animForgeWarpViz", "-evaluate"]
    parts += ["-host", '"%s"' % settings.host]
    parts += ["-port", str(int(settings.port))]
    parts += ["-characterId", '"%s"' % settings.character_id]
    parts += ["-clipName", '"%s"' % settings.clip_name]
    parts += ["-startFrame", "%g" % settings.start_frame]
    parts += ["-endFrame", "%g" % settings.end_frame]
    parts += ["-method", '"%s"' % settings.warp_method]
    parts += ["-warpTarget", '"%s"' % settings.warp_target_locator]
    parts += ["-warpRotation", "1" if settings.warp_rotation else "0"]
    parts += ["-warpTranslation", "1" if settings.warp_translation else "0"]
    parts += ["-ghostInterval", "%g" % settings.ghost_interval_frames]
    if settings.warp_window is not None:
        parts += ["-windowStart", "%g" % settings.warp_window[0]]
        parts += ["-windowEnd", "%g" % settings.warp_window[1]]
    return " ".join(parts) + ";"
