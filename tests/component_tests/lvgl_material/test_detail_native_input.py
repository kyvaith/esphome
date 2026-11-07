"""Public LVGL tests using product-derived geometry and initial visibility."""

from __future__ import annotations

import hashlib
import json
import os
import shlex
import subprocess
import unittest

from host_light_control import HERE, SOURCE, compiler_command
import test_number_native_input as number


class DetailNativeInputTests(unittest.TestCase):
    extra_widgets = {"arc"}
    # Product LVGL uses floating precise angles; its integer default hid an
    # invalid modulo expression until the ESP-IDF build.
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
        config = (cls.root / "lv_conf.h").read_text(encoding="utf8")
        for name, value in cls.lvgl_options.items():
            if f"#define {name} {value}" not in config:
                raise AssertionError(f"Unexpected cached LVGL option: {name}")
        data = json.loads(
            (HERE / "detail_widget_fixture.json").read_text(encoding="utf8")
        )
        lines = []
        for item in data:
            key = item["id"]
            parent = item["parent"]
            typ = item["type"]
            p = item["props"]
            constructor = {
                "button": "lv_button_create",
                "label": "lv_label_create",
                "arc": "lv_arc_create",
            }.get(typ, "lv_obj_create")
            v = f'w["{key}"]'
            lines.append(f'{v}={constructor}(w["{parent}"]);')
            if typ == "label":
                lines.append(f'lv_label_set_text({v},"");')
            lines.append(
                f"lv_obj_set_style_pad_all({v},0,0);lv_obj_set_style_border_width({v},0,0);"
            )
            for name in ("x", "y", "width", "height"):
                value = p.get(name)
                if isinstance(value, int):
                    lines.append(f"lv_obj_set_{name}({v},{value});")
                elif value == "SIZE_CONTENT":
                    lines.append(f"lv_obj_set_{name}({v},LV_SIZE_CONTENT);")
            for name, flag in (
                ("hidden", "HIDDEN"),
                ("clickable", "CLICKABLE"),
                ("scrollable", "SCROLLABLE"),
            ):
                if name in p:
                    lines.append(
                        f"lv_obj_{'add' if p[name] else 'remove'}_flag({v},LV_OBJ_FLAG_{flag});"
                    )
            if p.get("bg_opa") == "TRANSP":
                lines.append(f"lv_obj_set_style_bg_opa({v},LV_OPA_TRANSP,0);")
            if "align" in p:
                lines.append(f"lv_obj_set_align({v},LV_ALIGN_{p['align']});")
            if "start_angle" in p:
                lines.append(
                    f"lv_arc_set_bg_angles({v},{p['start_angle']},{p['end_angle']});"
                )
            if layout := p.get("layout"):
                lines.append(
                    f"lv_obj_set_flex_flow({v},LV_FLEX_FLOW_{layout['flex_flow']});"
                )
                lines.append(
                    f"lv_obj_set_flex_align({v},LV_FLEX_ALIGN_{layout['flex_align_main']},LV_FLEX_ALIGN_{layout['flex_align_cross']},LV_FLEX_ALIGN_{layout['flex_align_track']});"
                )
                lines.extend(
                    f"lv_obj_set_style_{field}({v},{layout[field]},0);"
                    for field in ("pad_row", "pad_column")
                    if field in layout
                )
        (cls.root / "detail_widgets.h").write_text("\n".join(lines), encoding="utf8")
        fixture = json.loads(
            (HERE / "detail_wire_fixtures.json").read_text(encoding="utf8")
        )
        (cls.root / "fixtures.h").write_text(
            "constexpr const char* FIXTURES[]={"
            + ",".join(json.dumps(x, ensure_ascii=False) for x in fixture)
            + "};",
            encoding="utf8",
        )
        reviews = json.loads(
            (HERE / "detail_review_fixtures.json").read_text(encoding="utf8")
        )
        (cls.root / "reviews.h").write_text(
            "constexpr const char* REVIEWS[][3]={"
            + ",".join(
                "{"
                + ",".join(json.dumps(x, ensure_ascii=False) for x in case)
                + ",nullptr}"
                if len(case) == 2
                else "{"
                + ",".join(json.dumps(x, ensure_ascii=False) for x in case)
                + "}"
                for case in reviews
            )
            + "};",
            encoding="utf8",
        )
        sources = [
            *[SOURCE / unit for unit in number.UNITS],
            *[
                SOURCE / ("material_entity_control_" + unit + ".cpp")
                for unit in (
                    "panel",
                    "panel_protocol",
                    "panel_component",
                    "detail",
                    "detail_protocol",
                    "detail_component",
                )
            ],
            *[SOURCE / unit for unit in getattr(cls, "extra_control_units", ())],
            HERE / getattr(cls, "fixture_source", "detail_native_input_test.cpp"),
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
            "-DUSE_LVGL_MATERIAL_DETAIL_CONTROLS",
            "-DUSE_LVGL_MATERIAL_THERMOSTAT",
            "-DUSE_LVGL_NAVIGATION_ADMISSION_GUARD",
            "-DARDUINOJSON_POOL_CAPACITY=128",
            "-DLV_CONF_INCLUDE_SIMPLE",
            "-DLV_COLOR_DEPTH=32",
        ]
        flags.extend(getattr(cls, "extra_control_flags", ()))
        objects = []
        for path in sources:
            target = cls.root / ("detail-" + path.name + ".o")
            cls.checked(
                [*compiler_command(), *flags, *cls.includes, "-c", path, "-o", target]
            )
            objects.append(target)
        executable = cls.root / ("detail-native" + (".exe" if os.name == "nt" else ""))
        arguments = [*map(str, [*objects, *cls.objects]), "-o", str(executable)]
        response = cls.root / "detail-native-link.rsp"
        response.write_text(
            subprocess.list2cmdline(arguments)
            if os.name == "nt"
            else shlex.join(arguments),
            encoding="utf8",
        )
        cls.checked([*compiler_command(), "@" + str(response)])
        return executable

    def test_product_geometry_pointer_and_lifecycle(self):
        for case in (
            "light_caps_revoke",
            "detail_semantic_revoke",
            "ha_recovery",
            "ha_passive",
            "ha_range",
            "light_scalar_handoff",
            "light_scalar_drain",
            "light_scalar_pending_close",
            "light_scalar_rejected",
            "light_scalar_timeout",
            "light_scalar_intent_aba",
            "light_scalar_intent_deadline",
            "light_power_aba",
            "light_power_release",
            "climate_open",
            "climate_drag",
            "climate_aba",
            "climate_current",
            "climate_passive",
            "climate_outage_status",
            "climate_outage_pending",
            "climate_outage_rejected",
            "climate_params",
            "climate_humidity_locale",
            "light_open",
            "light_kelvin",
            "light_numeric_unknown",
            "light_choice_empty",
            "light_choice_unknown",
            "facet_back",
            "choice_apply",
            "choice_aba",
            "choice_unknown",
            "choice_glyph",
            "facet_timeout",
            "reopen",
        ):
            with self.subTest(case=case):
                output = self.checked([self.executable, case])
                self.assertIn("RESULT " + case + " pass=1", output.stdout)
