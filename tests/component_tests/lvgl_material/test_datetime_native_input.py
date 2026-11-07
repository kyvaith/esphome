"""Actual public LVGL geometry, pointer capture and local date/time presentation."""

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
    "datetime",
    "date",
    "time",
    "save-open",
    "invalid",
    "switch-field",
    "cancel",
    "semantic-cancel",
    "metadata-hold",
    "retirement",
    "palette",
    "unfit",
    "unchanged",
    "interop",
)


def widgets_header():
    data = json.loads(
        (HERE / "datetime_widget_fixture.json").read_text(encoding="utf8")
    )
    rows = []
    for w in data["widgets"]:
        name = "w[" + json.dumps(w["id"]) + "]"
        parent = "w[" + json.dumps(w["parent"]) + "]"
        kind = w["type"]
        create = {
            "obj": "lv_obj_create",
            "label": "lv_label_create",
            "button": "lv_button_create",
            "svg_icon": "lv_obj_create",
        }[kind]
        rows.append(f"{name}={create}({parent});")
        if kind != "button":
            rows.append(f"lv_obj_remove_style_all({name});")
        rows.append(f"lv_obj_set_size({name},{w['width']},{w['height']});")
        if "align" in w:
            rows.append(f"lv_obj_align({name},LV_ALIGN_CENTER,0,0);")
        else:
            rows.append(f"lv_obj_set_pos({name},{w['x']},{w['y']});")
        rows.append(
            f"lv_obj_set_clickable({name},{str(kind == 'button').lower()});lv_obj_set_scrollable({name},false);"
        )
        for property in ("pad_all", "border_width", "shadow_width", "radius"):
            if property in w:
                rows.append(
                    f"lv_obj_set_style_{property}({name},{w[property]},LV_PART_MAIN);"
                )
        for property in ("bg_color", "text_color"):
            if property in w:
                rows.append(
                    f"lv_obj_set_style_{property}({name},lv_color_hex({w[property]}),LV_PART_MAIN);"
                )
        if "bg_opa" in w:
            rows.append(
                f"lv_obj_set_style_bg_opa({name},LV_OPA_{w['bg_opa']},LV_PART_MAIN);"
            )
        if w.get("hidden"):
            rows.append(f"lv_obj_add_flag({name},LV_OBJ_FLAG_HIDDEN);")
        if kind == "label":
            rows.append(
                f"lv_label_set_text({name},{json.dumps(w.get('text', ''))});lv_obj_set_style_text_font({name},host_font(),LV_PART_MAIN);"
            )
            rows.append(
                f"lv_label_set_long_mode({name},LV_LABEL_LONG_WRAP);lv_obj_set_style_text_align({name},LV_TEXT_ALIGN_CENTER,LV_PART_MAIN);"
            )
    p = data["profile"]
    ref = lambda key: "w[" + json.dumps(key) + "]"
    fields = (
        "root",
        "title",
        "kind",
        "summary",
        "zone",
        "updated",
        "status",
        "fields_root",
        "editor_root",
        "editor_field",
        "editor_digits",
    )
    rows.append(
        "control.set_datetime_profile(" + ",".join(ref(p[k]) for k in fields) + ");"
    )
    for i, k in enumerate(("date", "time")):
        rows.append(f"control.set_datetime_icon({i},{ref(p['icons'][k])});")
    for i, k in enumerate(("year", "month", "day", "hour", "minute", "second")):
        rows.append(
            f"control.set_datetime_field({i},"
            + ",".join(ref(p["fields"][k][v]) for v in ("button", "caption", "value"))
            + ");"
        )
    for i, k in enumerate(p["digits"] + [p["backspace"], p["accept"]]):
        rows.append(f"control.set_datetime_key({i},{ref(k)});")
    for i, k in enumerate(("minus", "plus", "cancel", "save")):
        rows.append(f"control.set_datetime_control({i},{ref(p['controls'][k])});")
    rows.append(
        f"control.set_datetime_labels({ref(p['cancel_label'])},{ref(p['save_label'])});"
    )
    for i, k in enumerate(("overview", "editor")):
        rows.append(
            f"control.set_datetime_field_layout({i},"
            + ",".join(
                str(p["field_layouts"][k][v])
                for v in ("x", "y", "height", "field_height")
            )
            + ");"
        )
    return "\n".join(rows)


def corpus_header():
    rows = json.loads((HERE / "datetime_wire_corpus.json").read_text(encoding="utf8"))[
        "messages"
    ]
    return (
        "struct Fixture {const char *name,*wire;}; const Fixture fixtures[]={\n"
        + ",\n".join(
            "{" + json.dumps(r["id"]) + "," + json.dumps(r["raw"]) + "}"
            for r in rows
            if r["message"]["kind"] == "grant"
        )
        + "};\n"
    )


class DatetimeNativeTests(unittest.TestCase):
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
        (cls.root / "datetime_widgets.h").write_text(widgets_header(), encoding="utf8")
        (cls.root / "datetime_fixtures.h").write_text(corpus_header(), encoding="utf8")
        names = [
            *number.UNITS,
            *[
                "material_entity_control_" + name + ".cpp"
                for name in (
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
                )
            ],
            *[name + ".cpp" for name in MEDIA_UNITS + ACTION_UNITS],
        ]
        sources = [SOURCE / name for name in dict.fromkeys(names)] + [
            HERE / "datetime_native_input_test.cpp"
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
            *[
                "-DUSE_LVGL_MATERIAL_" + name
                for name in (
                    "NUMBER_STEPPER",
                    "PANEL_CONTROLS",
                    "EXTRA_CONTROLS",
                    "INFO_CONTEXT",
                    "MEDIA_CONTROLS",
                    "ACTION_CONTROLS",
                    "DATETIME_CONTROLS",
                )
            ],
            "-DUSE_LVGL_NAVIGATION_ADMISSION_GUARD",
            "-DARDUINOJSON_POOL_CAPACITY=128",
            "-DLV_CONF_INCLUDE_SIMPLE",
            "-DLV_COLOR_DEPTH=32",
        ]
        objects = []
        for source in sources:
            target = cls.root / ("datetime-" + source.name + ".o")
            cls.checked(
                [*compiler_command(), *flags, *cls.includes, "-c", source, "-o", target]
            )
            objects.append(target)
        executable = cls.root / (
            "datetime-native" + (".exe" if os.name == "nt" else "")
        )
        arguments = [*map(str, [*objects, *cls.objects]), "-o", str(executable)]
        response = cls.root / "datetime-link.rsp"
        response.write_text(
            subprocess.list2cmdline(arguments)
            if os.name == "nt"
            else shlex.join(arguments),
            encoding="utf8",
        )
        cls.checked([*compiler_command(), "@" + str(response)])
        return executable

    def test_local_editor_pointer_and_retirement(self):
        for case in SCENARIOS:
            with self.subTest(case=case):
                self.assertIn(
                    "RESULT " + case + " pass=1",
                    self.checked([self.executable, case]).stdout,
                )
