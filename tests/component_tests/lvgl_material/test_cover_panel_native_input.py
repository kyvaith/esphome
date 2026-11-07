"""Absolute Cover rails and independent Stop contacts using real public LVGL."""

import test_cover_native_input as legacy


class CoverPanelNativeTests(legacy.CoverNativeInputTests):
    HARNESS = "cover_panel_native_input_test.cpp"
    EXTRA_FLAGS = (
        "-DUSE_LVGL_MATERIAL_COVER_PRIORITY_STOP",
        "-DUSE_LVGL_MATERIAL_COVER_PANEL",
    )

    def test_local_selection_apply_held_changes_and_domain_handoff(self):
        for case in (
            "open",
            "position",
            "tilt",
            "aba",
            "renewal",
            "stop",
            "tilt_stop",
            "unknown",
            "tilt_only",
            "read_only",
            "caption_capability",
            "legacy_restore",
            "reopen",
        ):
            with self.subTest(case=case):
                result = self.checked([self.executable, case])
                self.assertIn(f"COVER PANEL {case} pass=1", result.stdout)
