"""Tests for the skew-warp math (Python mirror of SkewWarpMath.h).

These cases document the contract of the shared math; the C++ test harness
(Tests/Cpp/TestMain.cpp) asserts the same properties so the two
implementations cannot drift silently.
"""

import math
import os
import sys
import unittest

_SCRIPTS = os.path.abspath(os.path.join(
    os.path.dirname(__file__), "..", "..", "UnrealGymProject", "Source", "Programs",
    "AnimForgeMayaWarpViz", "Scripts"))
sys.path.insert(0, _SCRIPTS)

import warpviz_skewwarp as skew
from warpviz_protocol import QUAT_IDENTITY, TrajectorySample


def straight_walk(frames=11, step=10.0, fps=30.0):
    """A straight walk down +Z, one sample per frame, root at origin."""
    return [
        TrajectorySample(i / fps, (0.0, 0.0, step * i), QUAT_IDENTITY)
        for i in range(frames)
    ]


def v_close(a, b, tol=1e-9):
    return all(abs(x - y) <= tol for x, y in zip(a, b))


class SkewMatrixTests(unittest.TestCase):

    def test_matrix_maps_original_delta_to_target_delta(self):
        d = (3.0, -1.0, 12.0)
        t = (5.0, 4.0, 9.0)
        w = skew.compute_skew_warp_matrix(d, t)
        self.assertTrue(v_close(skew.mat3_transform(w, d), t, tol=1e-12))

    def test_perpendicular_components_preserved(self):
        # The shear must be identity on the orthogonal complement of D.
        d = (0.0, 0.0, 10.0)
        t = (4.0, 0.0, 10.0)
        w = skew.compute_skew_warp_matrix(d, t)
        for perp in ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (2.5, -7.0, 0.0)):
            self.assertTrue(v_close(skew.mat3_transform(w, perp), perp, tol=1e-12))

    def test_identity_when_target_equals_original(self):
        d = (1.0, 2.0, 3.0)
        w = skew.compute_skew_warp_matrix(d, d)
        for v in ((1.0, 0.0, 0.0), (0.3, -2.0, 5.0)):
            self.assertTrue(v_close(skew.mat3_transform(w, v), v, tol=1e-12))

    def test_degenerate_original_delta_returns_identity(self):
        w = skew.compute_skew_warp_matrix((0.0, 0.0, 0.0), (5.0, 0.0, 0.0))
        self.assertTrue(v_close(skew.mat3_transform(w, (1.0, 2.0, 3.0)), (1.0, 2.0, 3.0)))


class WarpTrajectoryTests(unittest.TestCase):

    def _params(self, samples, target, **overrides):
        params = skew.WarpParams(
            window_start_seconds=samples[0].time_seconds,
            window_end_seconds=samples[-1].time_seconds,
            target_translation=target,
            warp_rotation=False,
        )
        for key, value in overrides.items():
            setattr(params, key, value)
        return params

    def test_endpoint_hits_target_exactly(self):
        samples = straight_walk()
        target = (50.0, 0.0, 130.0)
        warped = skew.warp_trajectory(samples, self._params(samples, target))
        self.assertTrue(v_close(warped[-1].translation, target, tol=1e-9))

    def test_start_of_window_unchanged(self):
        samples = straight_walk()
        warped = skew.warp_trajectory(samples, self._params(samples, (50.0, 0.0, 130.0)))
        self.assertTrue(v_close(warped[0].translation, samples[0].translation))

    def test_samples_before_window_untouched(self):
        samples = straight_walk(frames=21)
        params = self._params(samples, (40.0, 0.0, 160.0))
        params.window_start_seconds = samples[5].time_seconds
        params.window_end_seconds = samples[15].time_seconds
        warped = skew.warp_trajectory(samples, params)
        for i in range(5):
            self.assertTrue(v_close(warped[i].translation, samples[i].translation))
        # endpoint of the window still hits the target
        self.assertTrue(v_close(warped[15].translation, (40.0, 0.0, 160.0), tol=1e-9))

    def test_post_window_motion_carried_rigidly(self):
        # After the window, per-frame deltas must be preserved (no rotation
        # correction in this test), i.e. the tail is translated as a block.
        samples = straight_walk(frames=21)
        params = self._params(samples, (40.0, 0.0, 120.0))
        params.window_start_seconds = samples[0].time_seconds
        params.window_end_seconds = samples[10].time_seconds
        warped = skew.warp_trajectory(samples, params)
        for i in range(11, 21):
            original_delta = skew.v_sub(samples[i].translation, samples[i - 1].translation)
            warped_delta = skew.v_sub(warped[i].translation, warped[i - 1].translation)
            self.assertTrue(v_close(original_delta, warped_delta, tol=1e-9))

    def test_shape_preserved_lateral_weave(self):
        # A weaving walk whose lateral (X) weave closes at the window end, so
        # the net displacement D is purely +Z. The weave is then perpendicular
        # to D and SkewWarp must preserve it exactly.
        fps = 30.0
        samples = [
            TrajectorySample(i / fps, (5.0 * math.sin(i * math.pi / 5.0), 0.0, 10.0 * i),
                             QUAT_IDENTITY)
            for i in range(11)  # sin(i*pi/5) is 0 at i=0 and i=10
        ]
        target = (0.0, 0.0, 150.0)  # stretch Z only
        warped = skew.warp_trajectory(samples, self._params(samples, target))
        for original, result in zip(samples, warped):
            self.assertAlmostEqual(original.translation[0], result.translation[0], places=9)

    def test_degenerate_in_place_anim_ramps_offset(self):
        # Character doesn't move in the source: offset ramps in over the window.
        fps = 30.0
        samples = [TrajectorySample(i / fps, (0.0, 0.0, 0.0), QUAT_IDENTITY) for i in range(11)]
        target = (0.0, 0.0, 60.0)
        warped = skew.warp_trajectory(samples, self._params(samples, target))
        self.assertTrue(v_close(warped[0].translation, (0.0, 0.0, 0.0)))
        self.assertTrue(v_close(warped[-1].translation, target, tol=1e-9))
        z_values = [w.translation[2] for w in warped]
        self.assertEqual(z_values, sorted(z_values))  # monotonic ramp

    def test_scale_method_ignores_lateral_error(self):
        samples = straight_walk()
        target = (50.0, 0.0, 200.0)  # lateral 50 + stretch to 200
        params = self._params(samples, target, method="Scale")
        warped = skew.warp_trajectory(samples, params)
        # Scale only stretches along +Z; X error is not corrected.
        self.assertAlmostEqual(warped[-1].translation[0], 0.0, places=9)
        self.assertAlmostEqual(warped[-1].translation[2], 200.0, places=9)

    def test_rotation_correction_reaches_target(self):
        samples = straight_walk()
        half_sqrt2 = math.sqrt(0.5)
        target_rot = (0.0, half_sqrt2, 0.0, half_sqrt2)  # 90 deg yaw
        params = self._params(samples, (0.0, 0.0, 100.0),
                              warp_rotation=True, target_rotation=target_rot)
        warped = skew.warp_trajectory(samples, params)
        end = warped[-1].rotation
        dot = abs(sum(a * b for a, b in zip(end, target_rot)))
        self.assertGreater(dot, 1.0 - 1e-9)  # same orientation up to sign

    def test_rotation_correction_is_progressive(self):
        samples = straight_walk()
        half_sqrt2 = math.sqrt(0.5)
        params = self._params(samples, (0.0, 0.0, 100.0), warp_rotation=True,
                              target_rotation=(0.0, half_sqrt2, 0.0, half_sqrt2))
        warped = skew.warp_trajectory(samples, params)
        angles = []
        for sample in warped:
            w = max(-1.0, min(1.0, abs(sample.rotation[3])))
            angles.append(2.0 * math.acos(w))
        for previous, current in zip(angles, angles[1:]):
            self.assertGreaterEqual(current, previous - 1e-9)  # monotonic turn-in

    def test_empty_and_single_sample_inputs(self):
        self.assertEqual(skew.warp_trajectory([], self._params(
            [TrajectorySample(0.0, (0.0, 0.0, 0.0))], (1.0, 0.0, 0.0))), [])
        single = [TrajectorySample(0.0, (1.0, 2.0, 3.0))]
        warped = skew.warp_trajectory(single, self._params(single, (9.0, 9.0, 9.0)))
        self.assertTrue(v_close(warped[0].translation, (1.0, 2.0, 3.0)))

    def test_translation_disabled_keeps_positions(self):
        samples = straight_walk()
        params = self._params(samples, (50.0, 0.0, 130.0), warp_translation=False)
        warped = skew.warp_trajectory(samples, params)
        for original, result in zip(samples, warped):
            self.assertTrue(v_close(original.translation, result.translation))


if __name__ == "__main__":
    unittest.main()
