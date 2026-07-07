"""Python mirror of Shared/AnimForgeWarpVizShared/SkewWarpMath.h.

Used by mock_unreal_server.py so a warp evaluation can be simulated without
Unreal, and by the test suite to validate the math against known-good cases.
The formulas must stay 1:1 with the C++ header - see THEORY.md for the
derivation of the rank-one shear:

    W = I + (T - D) * D^T / (D . D)
"""

from __future__ import annotations

import math
from dataclasses import dataclass
from typing import List, Sequence, Tuple

from warpviz_protocol import QUAT_IDENTITY, Quat, TrajectorySample, Vec3

_EPS_SQ = 1e-12


# ---------------------------------------------------------------------------
# Small vector / quaternion helpers (tuples in, tuples out)
# ---------------------------------------------------------------------------

def v_add(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def v_sub(a: Vec3, b: Vec3) -> Vec3:
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def v_scale(a: Vec3, s: float) -> Vec3:
    return (a[0] * s, a[1] * s, a[2] * s)


def v_dot(a: Vec3, b: Vec3) -> float:
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def v_length(a: Vec3) -> float:
    return math.sqrt(v_dot(a, a))


def q_conjugate(q: Quat) -> Quat:
    return (-q[0], -q[1], -q[2], q[3])


def q_normalized(q: Quat) -> Quat:
    n = math.sqrt(sum(c * c for c in q))
    if n < 1e-12:
        return QUAT_IDENTITY
    return tuple(c / n for c in q)


def q_mul(a: Quat, b: Quat) -> Quat:
    """Hamilton product; (a * b) rotates by b first, then a (matches C++/UE)."""
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def q_rotate_vector(q: Quat, v: Vec3) -> Vec3:
    qv = (q[0], q[1], q[2])
    w = q[3]
    t = v_scale(_cross(qv, v), 2.0)
    return v_add(v_add(v, v_scale(t, w)), _cross(qv, t))


def _cross(a: Vec3, b: Vec3) -> Vec3:
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def q_slerp(a: Quat, b: Quat, alpha: float) -> Quat:
    cos_theta = sum(x * y for x, y in zip(a, b))
    to = b
    if cos_theta < 0.0:
        cos_theta = -cos_theta
        to = tuple(-c for c in b)
    if cos_theta > 0.9995:
        scale_a, scale_b = 1.0 - alpha, alpha
    else:
        theta = math.acos(cos_theta)
        inv_sin = 1.0 / math.sin(theta)
        scale_a = math.sin((1.0 - alpha) * theta) * inv_sin
        scale_b = math.sin(alpha * theta) * inv_sin
    return q_normalized(tuple(scale_a * x + scale_b * y for x, y in zip(a, to)))


# ---------------------------------------------------------------------------
# Skew warp
# ---------------------------------------------------------------------------

Mat3 = Tuple[Tuple[float, float, float], ...]


def compute_skew_warp_matrix(original_delta: Vec3, target_delta: Vec3) -> Mat3:
    """W = I + (T - D) D^T / (D . D). Identity when D is degenerate."""
    d_dot_d = v_dot(original_delta, original_delta)
    if d_dot_d < _EPS_SQ:
        return ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))
    correction = v_sub(target_delta, original_delta)
    inv = 1.0 / d_dot_d
    rows = []
    for r in range(3):
        rows.append(tuple(
            (1.0 if r == c else 0.0) + correction[r] * original_delta[c] * inv
            for c in range(3)))
    return tuple(rows)


def mat3_transform(m: Mat3, v: Vec3) -> Vec3:
    return tuple(m[r][0] * v[0] + m[r][1] * v[1] + m[r][2] * v[2] for r in range(3))


@dataclass
class WarpParams:
    window_start_seconds: float
    window_end_seconds: float
    target_translation: Vec3
    target_rotation: Quat = QUAT_IDENTITY
    method: str = "SkewWarp"
    warp_rotation: bool = True
    warp_translation: bool = True


def _arc_length_alpha(samples: Sequence[TrajectorySample], i0: int, i1: int, index: int) -> float:
    total = 0.0
    at_index = 0.0
    for i in range(i0 + 1, i1 + 1):
        total += v_length(v_sub(samples[i].translation, samples[i - 1].translation))
        if i <= index:
            at_index = total
    if total > 1e-9:
        return at_index / total
    time_span = samples[i1].time_seconds - samples[i0].time_seconds
    if time_span <= 1e-12:
        return 1.0 if index >= i1 else 0.0
    return (samples[index].time_seconds - samples[i0].time_seconds) / time_span


def warp_trajectory(samples: Sequence[TrajectorySample], params: WarpParams) -> List[TrajectorySample]:
    """Mirrors SkewWarpMath.h WarpTrajectory(). Returns a new sample list."""
    result = [TrajectorySample(s.time_seconds, s.translation, s.rotation) for s in samples]
    if not samples:
        return result

    i0 = None
    i1 = 0
    for i, s in enumerate(samples):
        if s.time_seconds >= params.window_start_seconds - 1e-9 and i0 is None:
            i0 = i
        if s.time_seconds <= params.window_end_seconds + 1e-9:
            i1 = i
    if i0 is None or i1 <= i0:
        return result

    origin = samples[i0].translation
    original_delta = v_sub(samples[i1].translation, origin)
    target_delta = v_sub(params.target_translation, origin)
    degenerate = v_dot(original_delta, original_delta) < _EPS_SQ

    warp_matrix = None
    scale_factor = 1.0
    if params.warp_translation and not degenerate:
        if params.method in ("SkewWarp", "SimpleWarp"):
            warp_matrix = compute_skew_warp_matrix(original_delta, target_delta)
        elif params.method == "Scale":
            scale_factor = v_dot(target_delta, original_delta) / v_dot(original_delta, original_delta)

    end_rotation = samples[i1].rotation
    rotation_correction = (
        q_normalized(q_mul(params.target_rotation, q_conjugate(end_rotation)))
        if params.warp_rotation else QUAT_IDENTITY)

    # --- warp samples inside the window ---------------------------------
    for i in range(i0, i1 + 1):
        local = v_sub(samples[i].translation, origin)
        warped_local = local
        if params.warp_translation:
            if degenerate:
                alpha = _arc_length_alpha(samples, i0, i1, i)
                warped_local = v_add(local, v_scale(target_delta, alpha))
            elif params.method == "Scale":
                warped_local = v_scale(local, scale_factor)
            else:
                warped_local = mat3_transform(warp_matrix, local)
        result[i].translation = v_add(origin, warped_local)

        if params.warp_rotation:
            alpha = _arc_length_alpha(samples, i0, i1, i)
            partial = q_slerp(QUAT_IDENTITY, rotation_correction, alpha)
            result[i].rotation = q_normalized(q_mul(partial, samples[i].rotation))

    # --- carry samples after the window rigidly -------------------------
    old_pivot = samples[i1].translation
    new_pivot = result[i1].translation
    for i in range(i1 + 1, len(samples)):
        from_pivot = v_sub(samples[i].translation, old_pivot)
        result[i].translation = v_add(new_pivot, q_rotate_vector(rotation_correction, from_pivot))
        result[i].rotation = q_normalized(q_mul(rotation_correction, samples[i].rotation))

    return result
