"""Host-test dependencies, always bound to this checkout's production sources."""

from __future__ import annotations

import importlib
import os
from pathlib import Path
import shlex
import shutil
import sys

import pytest

HERE = Path(__file__).resolve().parent
FORK = Path(__file__).resolve().parents[3]
SOURCE = FORK / "esphome/components/lvgl_material"
LVGL_SOURCE = FORK / "esphome/components/lvgl"


def compiler_command() -> list[str]:
    """Use the existing TileSurface compiler convention; never install tools."""
    if compiler := os.environ.get("ESPHOME_TEST_CXX"):
        if not Path(compiler).is_file() and shutil.which(compiler) is None:
            pytest.fail("ESPHOME_TEST_CXX does not identify an available compiler")
        return [compiler, *shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", ""))]
    compiler = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if compiler is None:
        pytest.skip("A host C++20 compiler is required")
    return [compiler]


def arduino_json() -> Path:
    """Public dependency supplied by the caller, without vendoring/cache lookup."""
    if not (configured := os.environ.get("ESPHOME_TEST_ARDUINOJSON")):
        pytest.skip("ESPHOME_TEST_ARDUINOJSON must point at ArduinoJson 7 src")
    source = Path(configured).resolve()
    if not (source / "ArduinoJson.h").is_file():
        pytest.fail("ESPHOME_TEST_ARDUINOJSON must contain ArduinoJson.h")
    version = source / "ArduinoJson/version.hpp"
    if (
        not version.is_file()
        or "#define ARDUINOJSON_VERSION_MAJOR 7"
        not in version.read_text(encoding="utf-8")
    ):
        pytest.fail("The fixed-arena codec fixture requires public ArduinoJson 7")
    return source


def load_native_component():
    """Reject silently testing an editable install from a different checkout."""
    sys.path.insert(0, str(FORK))
    module = importlib.import_module("esphome.components.lvgl_material")
    if Path(module.__file__).resolve() != (SOURCE / "__init__.py").resolve():
        raise RuntimeError(
            "Run pytest from this fork: lvgl_material resolved elsewhere"
        )
    return module
