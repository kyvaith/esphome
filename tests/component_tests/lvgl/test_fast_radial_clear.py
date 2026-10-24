"""Compare actual radial rasterization with the optional clear elision disabled."""

from __future__ import annotations

import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest

GUARD = "if (bounded_core && nearest_x * nearest_x + nearest_y * nearest_y > core_guard_squared)"
GEOMETRIES = (
    (192, 192),
    (384, 384),
    (193, 197),
    (383, 385),
    (192, 385),
    (385, 192),
    (97, 133),
    (64, 64),
)
SEQUENCES = (
    "sequential",
    "reverse",
    "skipped",
    "wrapped",
    "restart_recolor",
    "clamped",
)


@pytest.fixture(scope="module")
def pixel_results(tmp_path_factory: pytest.TempPathFactory) -> dict:
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
        pytest.skip("A host C++ compiler is required for the radial pixel oracle")
    root = Path(__file__).resolve().parents[3]
    source = (root / "esphome/components/lvgl/lottie_loader.h").read_text(
        encoding="utf8"
    )
    begin = source.index("static constexpr int32_t LOTTIE_FAST_RADIAL_COORD_SHIFT")
    end = source.index("// ThorVG owns only the work buffer", begin)
    actual = source[begin:end]
    assert actual.count(GUARD) == 1, (
        "Review the production guard before adapting its oracle"
    )
    baseline = actual.replace(GUARD, "if (false)", 1)
    output = tmp_path_factory.mktemp("radial_clear_geometry")
    for name, body in (("radial_before.inc", baseline), ("radial_after.inc", actual)):
        (output / name).write_text(
            body.replace("std::memset(", "counted_memset("), encoding="utf8"
        )
    executable = output / ("radial_clear.exe" if os.name == "nt" else "radial_clear")
    compiled = subprocess.run(
        [
            *command,
            "-std=c++17",
            "-O2",
            "-UNDEBUG",
            "-Wno-unknown-attributes",
            "-I",
            str(output),
            str(Path(__file__).with_name("fast_radial_clear_test.cpp")),
            "-o",
            str(executable),
        ],
        capture_output=True,
        text=True,
        timeout=180,
        check=False,
    )
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    result = subprocess.run(
        [str(executable)], capture_output=True, text=True, timeout=180, check=False
    )
    assert result.returncode == 0, result.stdout + result.stderr
    parsed = json.loads(result.stdout)
    assert parsed["frames_checked"] == 39264
    assert parsed["pixels_compared"] == 3217384899
    assert all(row["baseline_clear_bytes"] > 0 for row in parsed["results"])
    assert parsed["different_pixels"] == parsed["introduced_core_errors"] == 0
    return parsed


@pytest.mark.parametrize("width,height", GEOMETRIES)
@pytest.mark.parametrize("sequence", SEQUENCES)
@pytest.mark.parametrize("frame_span", [309, 360])
def test_radial_pixels_match_legacy_current_clear(
    pixel_results: dict, width: int, height: int, sequence: str, frame_span: int
) -> None:
    row = next(
        row
        for row in pixel_results["results"]
        if row["width"] == width
        and row["height"] == height
        and row["sequence"] == sequence
        and row["alignment"] == 1
        and row["frame_span"] == frame_span
    )
    assert row["different_pixels"] == 0
    assert row["candidate_core_errors"] == row["baseline_core_errors"]
    assert row["candidate_clear_bytes"] <= row["baseline_clear_bytes"]
    if min(width, height) >= 383:
        assert row["candidate_core_errors"] == 0


def test_radial_odd_stride_padding_and_guards(pixel_results: dict) -> None:
    row = next(row for row in pixel_results["results"] if row["alignment"] == 64)
    assert row["different_pixels"] == 0
    assert row["candidate_core_errors"] == row["baseline_core_errors"]


@pytest.mark.parametrize("width", [512, 768, 1024])
def test_radial_arithmetic_boundary_preserves_legacy(
    pixel_results: dict, width: int
) -> None:
    row = next(
        row
        for row in pixel_results["results"]
        if row["width"] == width and row["height"] == width
    )
    assert row["different_pixels"] == 0
    assert row["candidate_core_errors"] == row["baseline_core_errors"]
    if width >= 768:
        assert row["candidate_clear_bytes"] == row["baseline_clear_bytes"]
