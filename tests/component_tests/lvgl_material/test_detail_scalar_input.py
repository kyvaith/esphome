"""Real Scalar and LVGL regression for stable Detail Light presentation."""

import unittest

from test_detail_native_input import DetailNativeInputTests


class DetailScalarInputTests(unittest.TestCase):
    extra_widgets = DetailNativeInputTests.extra_widgets
    lvgl_options = DetailNativeInputTests.lvgl_options
    checked = staticmethod(DetailNativeInputTests.checked)
    setUpClass = classmethod(DetailNativeInputTests.setUpClass.__func__)
    tearDownClass = classmethod(DetailNativeInputTests.tearDownClass.__func__)
    build_control = classmethod(DetailNativeInputTests.build_control.__func__)
    fixture_source = "detail_scalar_input_test.cpp"
    extra_control_units = (
        "material_scalar_raster.cpp",
        "material_direct_scalar_control.cpp",
    )
    extra_control_flags = (
        "-DUSE_LVGL_MATERIAL_DIRECT_SCALAR_CONTROL",
        "-DUSE_LVGL_DIRECT_REGION_EXPECTED_GENERATION",
        "-DUSE_ESP32",
        "-DUSE_MIPI_DSI",
        "-DUSE_LVGL_PPA",
    )

    def test_unchanged_refresh_and_authoritative_brightness_completion(self):
        for case in (
            "addon_transition",
            "idle_known",
            "idle_unknown",
            "brightness",
            "brightness_capture",
            "brightness_refresh",
            "late_base_rejected",
            "retired_detail_recovery",
            "retired_legacy_recovery",
            "retired_inflight_recovery",
            "retired_number_recovery",
        ):
            with self.subTest(case=case):
                output = self.checked([self.executable, case])
                self.assertIn("RESULT " + case + " pass=1", output.stdout)
