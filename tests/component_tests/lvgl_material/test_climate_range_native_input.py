"""Range1 real LVGL pointers with exact product geometry and generated font metrics."""

from __future__ import annotations

import hashlib
import json
import os
import shlex
import subprocess
import unittest

from host_light_control import HERE, SOURCE, compiler_command
from test_entity_control_climate_range import fixtures
import test_number_native_input as number

SCENARIOS = (
    "main_pair",
    "main_negative",
    "main_long",
    "main_unknown",
    "main_inactive",
    "main_changed",
    "main_single",
    "open",
    "lower",
    "upper",
    "drag",
    "clamp",
    "cancel",
    "metadata",
    "aba",
    "same_generation",
    "readonly",
    "back",
    "range_disabled",
)


def widget_header(data):
    lines = [
        "static lv_style_t inset;lv_style_init(&inset);lv_style_set_border_width(&inset,0);lv_style_set_pad_all(&inset,0);"
    ]

    def val(k, v):
        if k in ("width", "height"):
            return (
                "LV_SIZE_CONTENT"
                if v == "SIZE_CONTENT"
                else f"lv_pct({v[:-1]})"
                if isinstance(v, str) and v.endswith("%")
                else str(v)
            )
        if k.endswith("color"):
            return f"lv_color_hex({v})"
        if k == "text_font":
            return f'font_for("{v}")'
        if k == "text_align":
            return "LV_TEXT_ALIGN_" + v
        if k.endswith("opa"):
            return (
                "LV_OPA_" + v
                if v in ("COVER", "TRANSP")
                else str(round(int(v[:-1]) * 255 / 100))
            )
        return str(v).lower() if isinstance(v, bool) else str(v)

    styles = (
        "radius",
        "arc_color",
        "arc_width",
        "arc_rounded",
        "width",
        "height",
        "opa",
        "bg_color",
        "bg_opa",
        "border_color",
        "border_width",
        "shadow_width",
        "text_color",
        "text_font",
        "text_align",
        "text_opa",
        "pad_all",
        "pad_top",
        "pad_bottom",
        "pad_left",
        "pad_right",
    )
    for item in data:
        p = item["props"]
        key = item["id"]
        v = f'w["{key}"]'
        typ = item["type"]
        constructor = {
            "button": "lv_button_create",
            "label": "lv_label_create",
            "arc": "lv_arc_create",
            "slider": "lv_slider_create",
        }.get(typ, "lv_obj_create")
        lines.append(f'{v}={constructor}(w["{item["parent"]}"]);')
        lines.append(
            f"lv_obj_set_style_pad_all({v},0,0);lv_obj_set_style_border_width({v},0,0);"
        )
        if typ == "label":
            lines.append(f'lv_label_set_text({v},"");')
        if p.get("styles"):
            assert p["styles"] == "m3_text_control_inset"
            lines.append(f"lv_obj_add_style({v},&inset,0);")
        for k in ("x", "y", "width", "height"):
            if k not in p:
                continue
            a = p[k]
            a = (
                "LV_SIZE_CONTENT"
                if a == "SIZE_CONTENT"
                else f"lv_pct({a[:-1]})"
                if isinstance(a, str) and a.endswith("%")
                else str(a)
            )
            lines.append(f"lv_obj_set_{k}({v},{a});")
        lines.extend(
            f"lv_obj_set_style_{k}({v},{val(k, p[k])},0);" for k in styles if k in p
        )
        for group, selector in (
            ("indicator", "LV_PART_INDICATOR"),
            ("knob", "LV_PART_KNOB"),
            ("pressed", "LV_STATE_PRESSED"),
            ("disabled", "LV_STATE_DISABLED"),
        ):
            for k, a in p.get(group, {}).items():
                assert k in styles, (group, k)
                lines.append(f"lv_obj_set_style_{k}({v},{val(k, a)},{selector});")
        for k, flag in (
            ("hidden", "HIDDEN"),
            ("clickable", "CLICKABLE"),
            ("scrollable", "SCROLLABLE"),
        ):
            if k in p:
                lines.append(
                    f"lv_obj_{'add' if p[k] else 'remove'}_flag({v},LV_OBJ_FLAG_{flag});"
                )
        if "align" in p:
            lines.append(f"lv_obj_set_align({v},LV_ALIGN_{p['align']});")
        if "flex_grow" in p:
            lines.append(f"lv_obj_set_flex_grow({v},{p['flex_grow']});")
        if "long_mode" in p:
            lines.append(
                f"lv_label_set_long_mode({v},LV_LABEL_LONG_MODE_{'DOTS' if p['long_mode'] == 'DOT' else p['long_mode']});"
            )
        if "start_angle" in p:
            lines.append(
                f"lv_arc_set_bg_angles({v},{p['start_angle']},{p['end_angle']});"
            )
        if typ == "slider":
            lines.append(
                f"lv_slider_set_range({v},{p.get('min_value', 0)},{p.get('max_value', 100)});lv_slider_set_value({v},{p.get('value', 0)},LV_ANIM_OFF);"
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
    return "\n".join(lines)


def font_header(data):
    lines = [
        "struct MetricGlyph {uint32_t code;uint16_t advance;};",
        "struct MetricFont {const MetricGlyph *glyphs;size_t size;};",
    ]
    for name, meta in data["fonts"].items():
        lines.append(
            "static const MetricGlyph glyphs_"
            + name
            + "[]={"
            + ",".join("{" + k + "," + str(v) + "}" for k, v in meta["glyphs"].items())
            + "};"
        )
        lines.append(
            f"static const MetricFont meta_{name}={{glyphs_{name},std::size(glyphs_{name})}};"
        )
    lines.append("""static bool metric_glyph(const lv_font_t *font,lv_font_glyph_dsc_t *out,uint32_t code,uint32_t next) {
      const auto &m=*static_cast<const MetricFont*>(font->dsc);
      for(size_t i=0;i<m.size;i++) { if(m.glyphs[i].code==code) {
        // Actual advance/line metrics; raster is a harmless host placeholder.
        if(!(LV_FONT_DEFAULT)->get_glyph_dsc(LV_FONT_DEFAULT,out,code<127?code:'M',next)) return false;
        out->adv_w=m.glyphs[i].advance;out->is_placeholder=false;return true;
      }}
      return false;
    }
    static const void *metric_bitmap(lv_font_glyph_dsc_t *dsc,lv_draw_buf_t *draw) {auto copy=*dsc;copy.resolved_font=LV_FONT_DEFAULT;return (LV_FONT_DEFAULT)->get_glyph_bitmap(&copy,draw);}
    static const lv_font_t *font_for(const char *name) {""")
    for name, m in data["fonts"].items():
        lines.append(
            f'if(std::strcmp(name,"{name}")==0) {{static lv_font_t f=*LV_FONT_DEFAULT;f.line_height={m["line_height"]};f.base_line={m["line_height"] - m["baseline"]};f.dsc=&meta_{name};f.get_glyph_dsc=metric_glyph;f.get_glyph_bitmap=metric_bitmap;return &f;}}'
        )
    lines.append("assert(false);return LV_FONT_DEFAULT;}")
    return "\n".join(lines)


class ClimateRangeNativeTests(unittest.TestCase):
    extra_widgets = {"arc", "slider", "bar"}
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
        geometry = json.loads(
            (HERE / "detail_widget_fixture.json").read_text(encoding="utf8")
        )
        color = json.loads(
            (HERE / "color_widget_fixture.json").read_text(encoding="utf8")
        )
        assert all(item in geometry for item in color)
        (cls.root / "range_widgets.h").write_text(
            widget_header(geometry), encoding="utf8"
        )
        (cls.root / "range_fonts.h").write_text(
            font_header(
                json.loads(
                    (HERE / "climate_range_font_metrics.json").read_text(
                        encoding="utf8"
                    )
                )
            ),
            encoding="utf8",
        )
        good, _ = fixtures()
        (cls.root / "range_fixtures.h").write_text(
            "constexpr const char* RANGE_FIXTURES[]={"
            + ",".join(
                json.dumps(
                    json.dumps(row, ensure_ascii=False, separators=(",", ":")),
                    ensure_ascii=False,
                )
                for row in good
            )
            + "};",
            encoding="utf8",
        )
        sources = [
            *[SOURCE / n for n in number.UNITS],
            *[
                SOURCE / ("material_entity_control_" + n + ".cpp")
                for n in (
                    "panel",
                    "panel_protocol",
                    "panel_component",
                    "detail",
                    "detail_protocol",
                    "detail_component",
                    "light_color",
                    "light_color_component",
                    "climate_range",
                )
            ],
            HERE / "climate_range_native_input_test.cpp",
        ]
        cls.pins.update(
            {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}
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
            "-DUSE_LVGL_MATERIAL_LIGHT_COLOR",
            "-DUSE_LVGL_MATERIAL_CLIMATE_RANGE",
            "-DUSE_LVGL_NAVIGATION_ADMISSION_GUARD",
            "-DARDUINOJSON_POOL_CAPACITY=128",
            "-DLV_CONF_INCLUDE_SIMPLE",
            "-DLV_COLOR_DEPTH=32",
        ]
        objects = []
        for p in sources:
            target = cls.root / ("color-" + p.name + ".o")
            cls.checked(
                [*compiler_command(), *flags, *cls.includes, "-c", p, "-o", target]
            )
            objects.append(target)
        executable = cls.root / ("color-native" + (".exe" if os.name == "nt" else ""))
        args = [*map(str, [*objects, *cls.objects]), "-o", str(executable)]
        rsp = cls.root / "color-native-link.rsp"
        rsp.write_text(
            subprocess.list2cmdline(args) if os.name == "nt" else shlex.join(args),
            encoding="utf8",
        )
        cls.checked([*compiler_command(), "@" + str(rsp)])
        return executable

    def test_range_pointer_lifecycle(self):
        for case in SCENARIOS:
            with self.subTest(case=case):
                self.assertIn(
                    "RESULT " + case + " pass=1",
                    self.checked([self.executable, case]).stdout,
                )
