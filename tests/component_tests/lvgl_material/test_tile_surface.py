"""Run production tile validation and gesture dispatch with real ArduinoJson."""

from __future__ import annotations

import asyncio
import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest


@pytest.fixture(scope="module")
def tile_executable(tmp_path_factory: pytest.TempPathFactory) -> Path:
    if compiler := os.environ.get("ESPHOME_TEST_CXX"):
        command = [compiler, *shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", ""))]
    else:
        compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
        if compiler is None:
            pytest.skip("A host C++ compiler is required")
        command = [compiler]
    arduino_json = os.environ.get("ESPHOME_TEST_ARDUINOJSON")
    if not arduino_json:
        pytest.skip("ESPHOME_TEST_ARDUINOJSON must point at ArduinoJson src")
    root = Path(__file__).resolve().parents[3]
    output = tmp_path_factory.mktemp("material_tile")
    for filename in ("material_tile_surface.h", "material_tile_surface.cpp"):
        source = (root / "esphome/components/lvgl_material" / filename).read_text(
            encoding="utf-8"
        )
        source = "\n".join(
            line for line in source.splitlines() if not line.startswith("#include")
        )
        (output / f"{filename}.inc").write_text(source, encoding="utf-8")
    executable = output / ("material_tile.exe" if os.name == "nt" else "material_tile")
    compiled = subprocess.run(
        [
            *command,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(output),
            "-I",
            arduino_json,
            str(Path(__file__).with_name("tile_surface_test.cpp")),
            "-o",
            str(executable),
        ],
        capture_output=True,
        text=True,
        timeout=180,
        check=False,
    )
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    return executable


@pytest.mark.parametrize("case", range(83))
def test_tile_surface_contract(tile_executable: Path, case: int) -> None:
    command = [str(tile_executable), str(case)]
    if case in (50, 51):
        command.append(
            str(
                Path(__file__).with_name("fixtures")
                / "native-readback-with-ha-accent.json"
            )
        )
    result = subprocess.run(
        command,
        capture_output=True,
        text=True,
        timeout=15,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert f"case {case} PASS" in result.stdout


def test_tile_schema_bounds_and_legacy_defaults() -> None:
    from esphome import config_validation as cv
    from esphome.components.lvgl_material import TILE_SURFACE_SCHEMA

    def binding(slot):
        return {
            "slot": slot,
            "widget": "button",
            "icon_label": "icon",
            "title_label": "title",
            "subtitle_label": "subtitle",
        }

    assert (
        TILE_SURFACE_SCHEMA({"tiles": [binding(255)]})["require_snapshot_ready"]
        is False
    )
    assert TILE_SURFACE_SCHEMA({"tiles": [binding(31)], "require_snapshot_ready": True})
    with pytest.raises(cv.Invalid, match="below 32"):
        TILE_SURFACE_SCHEMA({"tiles": [binding(32)], "require_snapshot_ready": True})
    with pytest.raises(cv.Invalid, match="unique"):
        TILE_SURFACE_SCHEMA({"tiles": [binding(2), binding(2)]})


def test_configure_accent_override_is_opt_in_and_accepts_black() -> None:
    from esphome import config_validation as cv
    from esphome.components.lvgl_material import TILE_SURFACE_CONFIGURE_SCHEMA
    from esphome.core import Lambda

    assert "accent_override" not in TILE_SURFACE_CONFIGURE_SCHEMA({"payload": "{}"})
    for color in (0, 0xFFFFFF, "0xAEE2FF", Lambda("return device_accent;")):
        assert "accent_override" in TILE_SURFACE_CONFIGURE_SCHEMA(
            {"payload": "{}", "accent_override": color}
        )
    for color in (-1, 0x1000000):
        with pytest.raises(cv.Invalid):
            TILE_SURFACE_CONFIGURE_SCHEMA({"payload": "{}", "accent_override": color})


def test_configure_codegen_preserves_absence_and_emits_zero_override(
    monkeypatch,
) -> None:
    from esphome.components import lvgl_material as material

    class GeneratedAction:
        def set_payload(self, value):
            return ("payload", value)

        def set_accent_override(self, value):
            return ("override", value)

    async def get_variable(_identifier):
        return object()

    async def templatable(value, _args, _type):
        return value

    generated = []
    monkeypatch.setattr(material.cg, "get_variable", get_variable)
    monkeypatch.setattr(material.cg, "templatable", templatable)
    monkeypatch.setattr(material.cg, "new_Pvariable", lambda *_args: GeneratedAction())
    monkeypatch.setattr(material.cg, "add", generated.append)
    asyncio.run(
        material.tile_surface_configure_to_code(
            {"id": "tiles", "payload": "{}"}, "action", (), []
        )
    )
    assert generated == [("payload", "{}")]
    generated.clear()
    asyncio.run(
        material.tile_surface_configure_to_code(
            {"id": "tiles", "payload": "{}", "accent_override": 0}, "action", (), []
        )
    )
    assert generated == [("payload", "{}"), ("override", 0)]


def test_control_schema_defaults_off_and_retains_legacy_high_slot():
    from esphome import config_validation as cv
    from esphome.components.lvgl_material import TILE_SURFACE_SCHEMA

    binding = {
        "slot": 255,
        "widget": "button",
        "icon_label": "icon",
        "title_label": "title",
        "subtitle_label": "subtitle",
    }
    default = TILE_SURFACE_SCHEMA({"tiles": [binding]})
    assert default["control_press_enabled"] is False
    assert default["require_snapshot_ready"] is False
    with pytest.raises(cv.Invalid, match="below 32"):
        TILE_SURFACE_SCHEMA({"tiles": [binding], "control_press_enabled": True})
    accepted = TILE_SURFACE_SCHEMA(
        {"tiles": [{**binding, "slot": 31}], "control_press_enabled": True}
    )
    assert accepted["control_press_enabled"] is True


@pytest.mark.parametrize(
    "action",
    [
        {"delay": "1ms"},
        {"wait_until": {"condition": {"lambda": "return true;"}}},
    ],
)
def test_control_schema_rejects_deferring_borrowed_identity_callbacks(action):
    from esphome import config_validation as cv
    from esphome.components.lvgl_material import TILE_SURFACE_SCHEMA
    from esphome.core import Lambda

    if "wait_until" in action:
        action = {"wait_until": {"condition": {"lambda": Lambda("return true;")}}}
    binding = {
        "slot": 2,
        "widget": "button",
        "icon_label": "icon",
        "title_label": "title",
        "subtitle_label": "subtitle",
    }
    with pytest.raises(cv.Invalid, match="synchronous|defer|borrowed"):
        TILE_SURFACE_SCHEMA(
            {"tiles": [binding], "on_control_press": [{"then": [action]}]}
        )


def test_control_schema_accepts_synchronous_callback_without_enabling_it():
    from esphome.components.lvgl_material import TILE_SURFACE_SCHEMA
    from esphome.core import Lambda

    binding = {
        "slot": 2,
        "widget": "button",
        "icon_label": "icon",
        "title_label": "title",
        "subtitle_label": "subtitle",
    }
    config = TILE_SURFACE_SCHEMA(
        {"tiles": [binding], "on_control_press": [{"lambda": Lambda("return;")}]}
    )
    assert config["control_press_enabled"] is False
    assert len(config["on_control_press"]) == 1


@pytest.mark.parametrize("enabled", [False, True])
def test_control_codegen_explicit_optin_and_captured_callback_types(
    monkeypatch, enabled
):
    from esphome.components import lvgl_material as material

    class GeneratedSurface:
        def set_require_snapshot_ready(self, value):
            return ("ready", value)

        def set_control_press_enabled(self, value):
            return ("control", value)

    class Context:
        async def __aenter__(self):
            return self

        async def __aexit__(self, *_args):
            return None

        def add(self, value):
            generated.append(value)

    async def register(*_args):
        return None

    async def callback(_var, method, args, conf):
        callbacks.append((method, args, conf))

    generated, callbacks = [], []
    monkeypatch.setattr(material.cg, "new_Pvariable", lambda *_args: GeneratedSurface())
    monkeypatch.setattr(material.cg, "register_component", register)
    monkeypatch.setattr(material.cg, "add", generated.append)
    monkeypatch.setattr(material.automation, "build_callback_automation", callback)
    monkeypatch.setattr(material, "LvContext", Context)
    config = {
        name: []
        for name in (
            "themes",
            "weather_presenters",
            "tile_surfaces",
            "notification_overlays",
            "voice_assistants",
            "wavy_progress",
            "scene_controllers",
            "entity_controls",
            "direct_scalar_controls",
            "direct_volume_overlays",
            "direct_marquees",
            "direct_spinners",
            "pressed_styles",
            "direct_state_layers",
            "state_layers",
            "page_indicators",
        )
    }
    config["tile_surfaces"] = [
        {
            "id": "tiles",
            "require_snapshot_ready": False,
            "control_press_enabled": enabled,
            "number_controls": False,
            "tiles": [],
            "on_control_press": [{"then": []}],
        }
    ]
    asyncio.run(material.to_code(config))
    assert generated == [("ready", False), ("control", enabled)]
    assert callbacks == [
        (
            "add_on_control_press_callback",
            [
                (material.cg.uint8, "slot"),
                (material.cg.const_char_ptr, "card_id"),
                (material.cg.uint64, "revision"),
            ],
            {"then": []},
        )
    ]
