"""Exercise the actual full-refresh SRM lifecycle with deterministic host stubs."""

from __future__ import annotations

import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest

from esphome.components.lvgl import CONFIG_SCHEMA
from esphome.const import KEY_CORE, KEY_TARGET_FRAMEWORK, KEY_TARGET_PLATFORM
from esphome.core import CORE


@pytest.mark.parametrize("configured", [None, False, True])
def test_direct_refresh_barrier_defaults_disabled(configured: bool | None) -> None:
    CORE.data[KEY_CORE] = {KEY_TARGET_PLATFORM: "host", KEY_TARGET_FRAMEWORK: ""}
    config = (
        {} if configured is None else {"direct_region_full_refresh_barrier": configured}
    )
    validated = CONFIG_SCHEMA(config)[0]
    assert validated["direct_region_full_refresh_barrier"] is (configured is True)


def method(source: str, name: str) -> str:
    start = source.index(f"void LvglComponent::{name}(")
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def test_actual_direct_refresh_lifecycle(tmp_path: Path) -> None:
    if compiler := os.environ.get("ESPHOME_TEST_CXX"):
        command = [compiler, *shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", ""))]
    elif compiler := os.environ.get("CXX"):
        command = shlex.split(compiler, posix=os.name != "nt")
    elif (
        compiler := shutil.which("c++")
        or shutil.which("g++")
        or shutil.which("clang++")
    ):
        command = [compiler]
    else:
        pytest.skip("A host C++ compiler is required for the refresh lifecycle")
    root = Path(__file__).resolve().parents[3]
    source = (root / "esphome/components/lvgl/lvgl_esphome.cpp").read_text(
        encoding="utf8"
    )
    names = (
        "direct_region_refresh_begin_",
        "direct_region_refresh_allow_compositor_",
        "direct_region_refresh_relock_",
        "direct_region_refresh_end_",
        "direct_region_refresh_end_cb",
    )
    (tmp_path / "actual_direct_refresh_methods.inc").write_text(
        "\n".join(method(source, name) for name in names), encoding="utf8"
    )
    # The same production cleanup must also handle LVGL's deleted-display abort,
    # where lv_display_refr_timer returns without emitting REFR_READY.
    assert "direct_region_refresh_end_cb, LV_EVENT_DELETE, this" in source
    executable = tmp_path / ("refresh.exe" if os.name == "nt" else "refresh")
    compile_result = subprocess.run(
        [
            *command,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(root),
            "-I",
            str(tmp_path),
            str(Path(__file__).with_name("direct_refresh_barrier_test.cpp")),
            "-o",
            str(executable),
        ],
        capture_output=True,
        text=True,
        timeout=180,
        check=False,
    )
    assert compile_result.returncode == 0, compile_result.stdout + compile_result.stderr
    result = subprocess.run(
        [str(executable)], capture_output=True, text=True, timeout=15, check=False
    )
    assert result.returncode == 0, result.stdout + result.stderr
