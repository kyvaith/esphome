"""Constant colors preserve public BGR8 values without runtime constructors."""

import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest

from esphome import config_validation as cv
from esphome.components.lvgl import lv_validation as lv
from esphome.core import CORE, ID


@pytest.mark.parametrize(
    ("value", "expected"),
    [
        (0x000000, "lv_color_t{0, 0, 0}"),
        (0xFFFFFF, "lv_color_t{255, 255, 255}"),
        (0x12AB34, "lv_color_t{52, 171, 18}"),
        (0xFF0000, "lv_color_t{0, 0, 255}"),
        (0x0000FF, "lv_color_t{255, 0, 0}"),
        (0x1000012, "lv_color_t{18, 0, 0}"),
        (-1, "lv_color_t{255, 255, 255}"),
    ],
)
def test_literal_channels_and_existing_byte_conversion(value: int, expected: str):
    """The old uint8_t function parameters also truncated accepted integers."""
    assert str(lv.color_retmapper(value)) == expected


def test_named_color():
    assert str(lv.color_retmapper("red")) == "lv_color_t{0, 0, 255}"


def test_configured_color_id(monkeypatch: pytest.MonkeyPatch):
    identifier = ID("accent", type=lv.ColorStruct, is_declaration=True)
    monkeypatch.setattr(
        CORE, "config", {"color": [{"id": identifier, "hex": (0x12, 0xAB, 0x34)}]}
    )
    assert str(lv.color_retmapper(identifier)) == "lv_color_t{52, 171, 18}"


def test_dynamic_color_expression_is_unchanged():
    expression = "return lv_color_make(red, green, blue);"
    result = lv.color_retmapper(cv.Lambda(expression))
    assert isinstance(result, cv.Lambda)
    assert result.value == expression


@pytest.mark.parametrize("color_depth", [16, 32])
def test_public_lvgl_color_layout(tmp_path: Path, color_depth: int):
    """Compile against real headers; a synthetic test typedef would prove nothing."""
    if not (configured := os.environ.get("ESPHOME_TEST_LVGL")):
        pytest.skip("ESPHOME_TEST_LVGL must identify the public LVGL sources")
    library = Path(configured)
    assert (library / "lvgl.h").is_file()
    compiler = os.environ.get("ESPHOME_TEST_CXX") or shutil.which("c++")
    if compiler is None:
        pytest.skip("A host C++ compiler is required")
    source = tmp_path / "literal_color_layout.cpp"
    source.write_text(
        """#include <cstddef>
#include "lvgl.h"
static_assert(sizeof(lv_color_t) == 3);
static_assert(offsetof(lv_color_t, blue) == 0);
static_assert(offsetof(lv_color_t, green) == 1);
static_assert(offsetof(lv_color_t, red) == 2);
constexpr lv_color_t sample{0x34, 0xAB, 0x12};
static_assert(sample.red == 0x12 && sample.green == 0xAB && sample.blue == 0x34);
""",
        encoding="utf-8",
    )
    result = subprocess.run(
        [
            compiler,
            *shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", "")),
            "-std=c++20",
            "-c",
            "-DLV_CONF_SKIP=1",
            f"-DLV_COLOR_DEPTH={color_depth}",
            f"-I{library}",
            str(source),
            "-o",
            str(tmp_path / "literal_color_layout.o"),
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
