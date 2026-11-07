"""Actual public LVGL input, full-text fit and retained Extra presentation."""

from __future__ import annotations

import hashlib
import json
import os
import shlex
import subprocess
import unittest

from host_light_control import HERE, SOURCE, compiler_command
import test_number_native_input as number

SCENARIOS = (
    "query-before-open",
    "sensor",
    "binary",
    "calendar",
    "alarm",
    "lock",
    "lock-palette",
    "unchanged",
    "missing-glyph",
    "overflow",
    "source-unknown",
    "lock-on-release",
    "lock-aba",
    "lock-outside",
    "retired-pending",
    "legacy-recovery",
    "unit-unsupported",
    "resized-unchanged",
    "calendar-caption-overflow",
    "lock-state-overflow",
)


def generate_widgets():
    """Use bounded fixture geometry; SVGs/fonts require separate device testing."""
    data = json.loads((HERE / "extra_widget_fixture.json").read_text(encoding="utf8"))
    lines = []
    for item in data:
        kind, p, key = item["type"], item["props"], item["id"]
        value = "w[" + json.dumps(key) + "]"
        create = {"label": "lv_label_create", "button": "lv_button_create"}.get(
            kind, "lv_obj_create"
        )
        lines.append(f"{value}={create}(w[{json.dumps(item['parent'])}]);")
        lines.append(f"lv_obj_remove_style_all({value});")
        lines.extend(
            f"lv_obj_set_{prop}({value},{p[prop]});"
            for prop in ("x", "y", "width", "height")
            if prop in p and isinstance(p[prop], int)
        )
        lines.extend(
            f"lv_obj_set_{prop}({value},{str(p[prop]).lower()});"
            for prop in ("hidden", "clickable", "scrollable")
            if prop in p
        )
        if "align" in p:
            lines.append(f"lv_obj_set_align({value},LV_ALIGN_{p['align']});")
        if p.get("bg_opa") == "COVER":
            lines.append(f"lv_obj_set_style_bg_opa({value},LV_OPA_COVER,0);")
        if "bg_color" in p:
            lines.append(
                f"lv_obj_set_style_bg_color({value},lv_color_hex({p['bg_color']}),0);"
            )
        if "radius" in p:
            lines.append(f"lv_obj_set_style_radius({value},{p['radius']},0);")
        if kind == "label":
            lines.append(
                f'lv_label_set_text({value},"");lv_obj_set_style_text_font({value},LV_FONT_DEFAULT,0);'
            )
            lines.append(
                f"lv_obj_set_style_text_color({value},lv_color_hex(0xffffff),0);"
            )
            lines.append(f"lv_label_set_long_mode({value},LV_LABEL_LONG_MODE_WRAP);")
            if "text_align" in p:
                lines.append(
                    f"lv_obj_set_style_text_align({value},LV_TEXT_ALIGN_{p['text_align']},0);"
                )
    return "\n".join(lines)


class ExtraNativeInputTests(unittest.TestCase):
    # Match the product floating precise angle configuration while using the
    # established public LVGL native-input harness and owned temporary files.
    extra_widgets = {"arc"}
    lvgl_options = {"LV_USE_FLOAT": 1}
    checked = staticmethod(number.NumberNativeInputTests.checked)

    @classmethod
    def setUpClass(cls):
        number.NumberNativeInputTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        number.NumberNativeInputTests.tearDownClass.__func__(cls)

    @classmethod
    def build_control(cls):
        (cls.root / "extra_widgets.h").write_text(generate_widgets(), encoding="utf8")
        sources = [
            *[SOURCE / unit for unit in number.UNITS],
            *[
                SOURCE / ("material_entity_control_" + unit + ".cpp")
                for unit in (
                    "panel",
                    "panel_protocol",
                    "panel_component",
                    "extra",
                    "extra_protocol",
                    "extra_component",
                )
            ],
            HERE / "extra_native_input_test.cpp",
        ]
        cls.pins.update(
            {path: hashlib.sha256(path.read_bytes()).hexdigest() for path in sources}
        )
        flags = [
            "-std=c++20",
            "-UNDEBUG",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-deprecated-declarations",
            "-DUSE_LVGL_MATERIAL_NUMBER_STEPPER",
            "-DUSE_LVGL_MATERIAL_PANEL_CONTROLS",
            "-DUSE_LVGL_MATERIAL_EXTRA_CONTROLS",
            "-DUSE_LVGL_NAVIGATION_ADMISSION_GUARD",
            "-DARDUINOJSON_POOL_CAPACITY=128",
            "-DLV_CONF_INCLUDE_SIMPLE",
            "-DLV_COLOR_DEPTH=32",
        ]
        objects = []
        for path in sources:
            target = cls.root / ("extra-" + path.name + ".o")
            cls.checked(
                [*compiler_command(), *flags, *cls.includes, "-c", path, "-o", target]
            )
            objects.append(target)
        executable = cls.root / ("extra-native" + (".exe" if os.name == "nt" else ""))
        arguments = [*map(str, [*objects, *cls.objects]), "-o", str(executable)]
        response = cls.root / "extra-native-link.rsp"
        response.write_text(
            subprocess.list2cmdline(arguments)
            if os.name == "nt"
            else shlex.join(arguments),
            encoding="utf8",
        )
        cls.checked([*compiler_command(), "@" + str(response)])
        return executable

    def test_profiles_observation_fit_and_native_input_retirement(self):
        for case in SCENARIOS:
            with self.subTest(case=case):
                output = self.checked([self.executable, case])
                self.assertIn("RESULT " + case + " pass=1", output.stdout)
