"""Tests for warpviz_session (UI-independent Evaluate logic)."""

import os
import sys
import unittest

_SCRIPTS = os.path.abspath(os.path.join(
    os.path.dirname(__file__), "..", "..", "UnrealGymProject", "Source", "Programs",
    "AnimForgeMayaWarpViz", "Scripts"))
sys.path.insert(0, _SCRIPTS)

from warpviz_protocol import TrajectorySample, WarpTarget
from warpviz_session import SessionSettings, build_evaluate_command, build_evaluate_request, validate_settings


def valid_settings(**overrides):
    settings = SessionSettings(
        character_id="AF_Mannequin",
        clip_name="MM_Vault_Low",
        start_frame=1.0,
        end_frame=60.0,
        fps=30.0,
        warp_method="SkewWarp",
        warp_target_locator="warpTarget_loc",
    )
    for key, value in overrides.items():
        setattr(settings, key, value)
    return settings


class ValidateSettingsTests(unittest.TestCase):

    def test_valid_settings_pass(self):
        result = validate_settings(valid_settings())
        self.assertTrue(result.ok, result.errors)

    def test_inverted_range_rejected(self):
        result = validate_settings(valid_settings(start_frame=60.0, end_frame=10.0))
        self.assertFalse(result.ok)
        self.assertIn("End frame", result.errors[0])

    def test_unknown_method_rejected(self):
        result = validate_settings(valid_settings(warp_method="TeleportWarp"))
        self.assertFalse(result.ok)

    def test_missing_target_rejected(self):
        result = validate_settings(valid_settings(warp_target_locator=""))
        self.assertFalse(result.ok)

    def test_missing_clip_and_character_rejected(self):
        result = validate_settings(valid_settings(clip_name="", character_id=""))
        self.assertEqual(len(result.errors), 2)

    def test_window_outside_range_rejected(self):
        result = validate_settings(valid_settings(warp_window=(0.0, 90.0)))
        self.assertFalse(result.ok)

    def test_inverted_window_rejected(self):
        result = validate_settings(valid_settings(warp_window=(40.0, 20.0)))
        self.assertFalse(result.ok)

    def test_window_inside_range_ok(self):
        result = validate_settings(valid_settings(warp_window=(10.0, 50.0)))
        self.assertTrue(result.ok, result.errors)

    def test_nothing_to_warp_rejected(self):
        result = validate_settings(valid_settings(warp_rotation=False, warp_translation=False))
        self.assertFalse(result.ok)

    def test_bad_port_rejected(self):
        result = validate_settings(valid_settings(port=0))
        self.assertFalse(result.ok)

    def test_zero_ghost_interval_warns_but_passes(self):
        result = validate_settings(valid_settings(ghost_interval_frames=0.0))
        self.assertTrue(result.ok)
        self.assertTrue(result.warnings)

    def test_huge_range_warns_but_passes(self):
        result = validate_settings(valid_settings(end_frame=5000.0))
        self.assertTrue(result.ok)
        self.assertTrue(any("frames" in w for w in result.warnings))


class BuildRequestTests(unittest.TestCase):

    def test_request_carries_settings(self):
        settings = valid_settings(warp_window=(10.0, 50.0))
        target = WarpTarget("warpTarget_loc", (1.0, 2.0, 3.0))
        samples = [TrajectorySample(0.0, (0.0, 0.0, 0.0))]
        request = build_evaluate_request(settings, target, samples)
        self.assertEqual(request.clip_name, "MM_Vault_Low")
        self.assertEqual(request.range.start_frame, 1.0)
        self.assertEqual(request.warp_window.start_frame, 10.0)
        self.assertEqual(request.warp_window.end_frame, 50.0)
        self.assertEqual(request.target.translation, (1.0, 2.0, 3.0))
        self.assertEqual(len(request.request_id), 32)

    def test_window_defaults_to_range(self):
        request = build_evaluate_request(valid_settings(), WarpTarget("loc"), [])
        self.assertEqual(request.warp_window.start_frame, request.range.start_frame)
        self.assertEqual(request.warp_window.end_frame, request.range.end_frame)

    def test_request_ids_unique(self):
        settings = valid_settings()
        first = build_evaluate_request(settings, WarpTarget("loc"), [])
        second = build_evaluate_request(settings, WarpTarget("loc"), [])
        self.assertNotEqual(first.request_id, second.request_id)


class BuildCommandTests(unittest.TestCase):

    def test_command_contains_all_flags(self):
        command = build_evaluate_command(valid_settings(warp_window=(10.0, 50.0)))
        for token in ("animForgeWarpViz", "-evaluate", '-clipName "MM_Vault_Low"',
                      '-method "SkewWarp"', '-warpTarget "warpTarget_loc"',
                      "-startFrame 1", "-endFrame 60",
                      "-windowStart 10", "-windowEnd 50"):
            self.assertIn(token, command)
        self.assertTrue(command.endswith(";"))

    def test_command_omits_window_when_unset(self):
        command = build_evaluate_command(valid_settings())
        self.assertNotIn("-windowStart", command)

    def test_command_encodes_booleans(self):
        command = build_evaluate_command(valid_settings(warp_rotation=False))
        self.assertIn("-warpRotation 0", command)
        self.assertIn("-warpTranslation 1", command)


if __name__ == "__main__":
    unittest.main()
