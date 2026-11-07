"""Actual public LVGL pointer, keyboard, text viewport and retirement tests."""

import hashlib
import json
import os
import shlex
import subprocess
import unittest
from host_light_control import HERE, SOURCE, compiler_command
from test_entity_control_media import MEDIA_UNITS
from test_entity_control_action import ACTION_UNITS
import test_number_native_input as number

SCENARIOS = (
    "overview",
    "empty",
    "edit",
    "polish",
    "invalid",
    "wrap",
    "slide",
    "cancel",
    "semantic",
    "metadata",
    "privacy",
    "password",
    "unchanged",
    "cursor",
    "palette",
    "unfit",
    "allocation",
)


def widgets_header():
    data = json.loads((HERE / "text_widget_fixture.json").read_text(encoding="utf8"))
    rows = []
    # Keep named styles shared in the host tree instead of flattening them into
    # local properties: allocation and precedence are part of this fixture.
    for style in data.get("style_definitions", []):
        assert set(style) <= {"id", "border_width", "pad_all"}, style
        name = style["id"]
        rows += [f"static lv_style_t {name};lv_style_init(&{name});"]
        for prop in ("border_width", "pad_all"):
            if prop in style:
                rows += [f"lv_style_set_{prop}(&{name},{style[prop]});"]
    ref = lambda k: "w[" + json.dumps(k) + "]"
    for w in data["widgets"]:
        name, parent = ref(w["id"]), ref(w["parent"])
        kind = w["type"]
        create = {
            "obj": "lv_obj_create",
            "label": "lv_label_create",
            "button": "lv_button_create",
            "svg_icon": "lv_obj_create",
            "buttonmatrix": "lv_buttonmatrix_create",
        }[kind]
        rows += [f"{name}={create}({parent});"]
        if kind not in ("button", "buttonmatrix"):
            rows += [f"lv_obj_remove_style_all({name});"]
        for style in w.get("styles", []):
            rows += [f"lv_obj_add_style({name},&{style},LV_PART_MAIN);"]
        rows += [
            f"lv_obj_set_size({name},{w['width']},{w['height']});",
            f"lv_obj_set_pos({name},{w['x']},{w['y']});",
            f"lv_obj_set_clickable({name},{str(w.get('clickable', False)).lower()});lv_obj_set_scrollable({name},false);",
        ]
        for prop in (
            "pad_all",
            "pad_column",
            "pad_row",
            "border_width",
            "shadow_width",
            "radius",
        ):
            if prop in w:
                rows += [f"lv_obj_set_style_{prop}({name},{w[prop]},LV_PART_MAIN);"]
        for prop in ("bg_color", "text_color"):
            if prop in w:
                rows += [
                    f"lv_obj_set_style_{prop}({name},lv_color_hex({w[prop]}),LV_PART_MAIN);"
                ]
        if "bg_opa" in w:
            rows += [
                f"lv_obj_set_style_bg_opa({name},LV_OPA_{w['bg_opa']},LV_PART_MAIN);"
            ]
        if w.get("hidden"):
            rows += [f"lv_obj_add_flag({name},LV_OBJ_FLAG_HIDDEN);"]
        if kind in ("label", "buttonmatrix"):
            rows += [f"lv_obj_set_style_text_font({name},host_font(),LV_PART_MAIN);"]
        if kind == "label":
            rows += [
                f"lv_label_set_text({name},{json.dumps(w.get('text', ''))});lv_label_set_long_mode({name},LV_LABEL_LONG_{w.get('long_mode', 'CLIP')});lv_obj_set_style_text_align({name},LV_TEXT_ALIGN_{w.get('text_align', 'LEFT')},LV_PART_MAIN);"
            ]
        for prop, value in w.get("items", {}).items():
            if prop.endswith("_color"):
                value = "lv_color_hex(" + value + ")"
            elif prop == "bg_opa":
                value = "LV_OPA_" + value
            rows += [f"lv_obj_set_style_{prop}({name},{value},LV_PART_ITEMS);"]
    p = data["profile"]
    fields = (
        "root",
        "title",
        "kind",
        "icon",
        "surface",
        "viewport",
        "cursor",
        "count",
        "status",
        "cancel_label",
        "save_label",
    )
    rows += ["control.set_text_profile(" + ",".join(ref(p[k]) for k in fields) + ");"]
    for i, k in enumerate(p["lines"]):
        rows += [f"control.set_text_line({i},{ref(k)});"]
    for i, k in enumerate(p["keyboard"]):
        rows += [f"control.set_text_keyboard({i},{ref(k)},{p['row_lengths'][i]});"]
    for i, k in enumerate(
        (
            "up",
            "down",
            "shift",
            "backspace",
            "symbols",
            "alternate",
            "left",
            "space",
            "right",
            "cancel",
            "save",
        )
    ):
        rows += [f"control.set_text_control({i},{ref(p['controls'][k])});"]
    for i, k in enumerate(("symbols", "alternate", "space")):
        rows += [f"control.set_text_key_label({i},{ref(p['key_labels'][k])});"]
    for m, keys in enumerate(p["maps"]):
        offset = 0
        for row, count in enumerate(p["row_lengths"]):
            keysrow = keys[offset : offset + count]
            name = f"keys_{m}_{row}"
            offset += count
            rows += [
                "static const char *const "
                + name
                + "[]={"
                + ",".join(json.dumps(k["label"], ensure_ascii=False) for k in keysrow)
                + ',""};',
                "static const uint32_t "
                + name
                + "_points[]={"
                + ",".join(str(ord(k["value"])) if k["value"] else "0" for k in keysrow)
                + "};",
                f"control.set_text_map({m},{row},{name},{name}_points);",
            ]
    rows += [
        "static const uint32_t repertoire[]={"
        + ",".join(str(ord(c)) for c in sorted(p["repertoire"]))
        + "};",
        f"control.set_text_repertoire(repertoire,{len(p['repertoire'])});",
    ]
    for i, k in enumerate(("overview", "editor")):
        rows += [
            f"control.set_text_layout({i},"
            + ",".join(
                str(p["layouts"][k][v])
                for v in (
                    "surface_y",
                    "surface_height",
                    "viewport_y",
                    "visible_lines",
                    "count_y",
                    "up_y",
                    "down_y",
                    "save_x",
                )
            )
            + ");"
        ]
    rows += [f"control.set_text_line_height({p['line_height']});"]
    return "\n".join(rows)


def corpus_header():
    rows = json.loads((HERE / "text_ui_wire_corpus.json").read_text(encoding="utf8"))
    return (
        "struct Fixture {const char *name,*wire;}; const Fixture fixtures[]={\n"
        + ",\n".join(
            "{"
            + json.dumps(r["name"])
            + ","
            + json.dumps(r["json"], ensure_ascii=False)
            + "}"
            for r in rows
            if r["value"]["kind"] == "grant"
        )
        + "};\n"
    )


class TextNativeTests(unittest.TestCase):
    extra_widgets = {"arc", "buttonmatrix"}
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
        (cls.root / "text_widgets.h").write_text(widgets_header(), encoding="utf8")
        (cls.root / "text_fixtures.h").write_text(corpus_header(), encoding="utf8")
        names = [
            *number.UNITS,
            *[
                "material_entity_control_" + n + ".cpp"
                for n in (
                    "panel",
                    "panel_protocol",
                    "panel_component",
                    "extra",
                    "extra_protocol",
                    "extra_component",
                    "info_context",
                    "info_context_component",
                    "media_component",
                    "action_component",
                    "datetime",
                    "datetime_protocol",
                    "datetime_owner",
                    "datetime_component",
                    "text",
                    "text_protocol",
                    "text_owner",
                    "text_component",
                )
            ],
            *[n + ".cpp" for n in MEDIA_UNITS + ACTION_UNITS],
        ]
        sources = [SOURCE / n for n in dict.fromkeys(names)] + [
            HERE / "text_native_input_test.cpp"
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
            *[
                "-DUSE_LVGL_MATERIAL_" + n
                for n in (
                    "NUMBER_STEPPER",
                    "PANEL_CONTROLS",
                    "EXTRA_CONTROLS",
                    "INFO_CONTEXT",
                    "MEDIA_CONTROLS",
                    "ACTION_CONTROLS",
                    "DATETIME_CONTROLS",
                    "TEXT_CONTROLS",
                )
            ],
            "-DUSE_LVGL_NAVIGATION_ADMISSION_GUARD",
            "-DARDUINOJSON_POOL_CAPACITY=128",
            "-DLV_CONF_INCLUDE_SIMPLE",
            "-DLV_COLOR_DEPTH=32",
        ]
        objects = []
        for source in sources:
            target = cls.root / ("text-" + source.name + ".o")
            cls.checked(
                [*compiler_command(), *flags, *cls.includes, "-c", source, "-o", target]
            )
            objects.append(target)
        exe = cls.root / ("text-native" + (".exe" if os.name == "nt" else ""))
        arguments = [*map(str, [*objects, *cls.objects]), "-o", str(exe)]
        rsp = cls.root / "text-link.rsp"
        rsp.write_text(
            subprocess.list2cmdline(arguments)
            if os.name == "nt"
            else shlex.join(arguments),
            encoding="utf8",
        )
        cls.checked([*compiler_command(), "@" + str(rsp)])
        return exe

    def test_public_text_input(self):
        for case in SCENARIOS:
            with self.subTest(case=case):
                self.assertIn(
                    "RESULT " + case + " pass=1",
                    self.checked([self.executable, case]).stdout,
                )
