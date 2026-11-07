"""Actual public LVGL context display, read-only input and retained lifecycle."""

import hashlib
import json
import os
import shlex
import subprocess

from host_light_control import HERE, SOURCE, compiler_command
import test_extra_native_input as extra
import test_info_context as codec
import test_number_native_input as number

SCENARIOS = (
    "person-date", "person-palette", "sun-values", "sun-unavailable",
    "person-fit", "person-lifecycle", "person-readonly", "sun-readonly",
)


def generate_widgets():
    data = json.loads((HERE / "info_context_widget_fixture.json").read_text("utf8"))
    lines = []
    for family in ("person", "sun"):
        fields = data["schema"]["extra_controller"][family]["widget_fields"]
        for field in fields:
            key = f'{family}_{field}'
            label = field not in ("root", "icon", "surface")
            parent = "extra_control_root" if field == "root" else f"{family}_root"
            value = f'w["{key}"]'
            create = "lv_label_create" if label else "lv_obj_create"
            lines.append(f'{value}={create}(w["{parent}"]);lv_obj_remove_style_all({value});')
            x, y, width, height = data["geometry"][family][field]
            lines.append(f"lv_obj_set_pos({value},{x},{y});lv_obj_set_size({value},{width},{height});")
            if label:
                lines.append(f'lv_label_set_text({value},"");lv_obj_set_style_text_font({value},context_font(),0);')
                lines.append(f"lv_label_set_long_mode({value},LV_LABEL_LONG_MODE_WRAP);")
            else:
                lines.append(f"lv_obj_set_style_bg_opa({value},LV_OPA_COVER,0);")
    # The new product uses a two-line shared footer. Old type strings stay unchanged.
    x, y, width, height = data["geometry"]["common"]["updated"]
    lines.append(f'lv_obj_set_pos(w["extra_control_updated"],{x},{y});lv_obj_set_size(w["extra_control_updated"],{width},{height});')
    lines.append('lv_obj_set_style_text_font(w["extra_control_updated"],context_font(),0);')
    return "\n".join(lines)


class InfoContextNativeTests(extra.ExtraNativeInputTests):
    @classmethod
    def build_control(cls):
        (cls.root / "extra_widgets.h").write_text(extra.generate_widgets(), encoding="utf8")
        (cls.root / "info_context_widgets.h").write_text(generate_widgets(), encoding="utf8")
        (cls.root / "context_corpus.h").write_text(codec.corpus_header(), encoding="utf8")
        sources = [
            *[SOURCE / unit for unit in number.UNITS],
            *[SOURCE / ("material_entity_control_" + unit + ".cpp") for unit in (
                "panel", "panel_protocol", "panel_component", "extra", "extra_protocol", "extra_component",
                "info_context", "info_context_component",
            )],
            HERE / "info_context_native_test.cpp",
        ]
        cls.pins.update({path: hashlib.sha256(path.read_bytes()).hexdigest() for path in sources})
        flags = [
            "-std=c++20", "-UNDEBUG", "-Wall", "-Wextra", "-Werror", "-Wno-deprecated-declarations",
            "-DUSE_LVGL_MATERIAL_NUMBER_STEPPER", "-DUSE_LVGL_MATERIAL_PANEL_CONTROLS",
            "-DUSE_LVGL_MATERIAL_EXTRA_CONTROLS", "-DUSE_LVGL_MATERIAL_INFO_CONTEXT",
            "-DUSE_LVGL_NAVIGATION_ADMISSION_GUARD", "-DARDUINOJSON_POOL_CAPACITY=128",
            "-DLV_CONF_INCLUDE_SIMPLE", "-DLV_COLOR_DEPTH=32",
        ]
        objects = []
        for path in sources:
            target = cls.root / ("context-" + path.name + ".o")
            cls.checked([*compiler_command(), *flags, *cls.includes, "-c", path, "-o", target])
            objects.append(target)
        executable = cls.root / ("context-native" + (".exe" if os.name == "nt" else ""))
        arguments = [*map(str, [*objects, *cls.objects]), "-o", str(executable)]
        response = cls.root / "context-native-link.rsp"
        response.write_text(subprocess.list2cmdline(arguments) if os.name == "nt" else shlex.join(arguments), encoding="utf8")
        cls.checked([*compiler_command(), "@" + str(response)])
        return executable

    def test_profiles_observation_fit_and_native_input_retirement(self):
        for case in SCENARIOS:
            with self.subTest(case=case):
                output = self.checked([self.executable, case])
                self.assertIn("RESULT " + case + " pass=1", output.stdout)
