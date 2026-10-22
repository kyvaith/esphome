"""Execute production JPEG cache admission with deterministic codec/owner fakes."""

from __future__ import annotations

import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess

import pytest


def production_function(source: str, signature: str) -> str:
    """Select a definition rather than its forward declaration."""
    match = re.search(re.escape(signature) + r"[^;{]*\{", source)
    assert match is not None, signature
    start = match.start()
    opening = source.index("{", start)
    depth = 1
    cursor = opening + 1
    while depth:
        depth += (source[cursor] == "{") - (source[cursor] == "}")
        cursor += 1
    return source[start:cursor] + "\n"


@pytest.fixture(scope="module")
def jpeg_admission_executables(
    tmp_path_factory: pytest.TempPathFactory,
) -> dict[str, Path]:
    if compiler := os.environ.get("ESPHOME_TEST_CXX"):
        command = [
            compiler,
            *shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", "")),
        ]
    else:
        compiler = os.environ.get("CXX")
        command = shlex.split(compiler, posix=os.name != "nt") if compiler else []
    if not command:
        available = (
            shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
        )
        if available is None:
            pytest.skip("A host C++ compiler is required for JPEG cache admission")
        command = [available]
    root = Path(__file__).resolve().parents[3]
    source = (root / "esphome/components/lvgl/lvgl_esphome.cpp").read_text(
        encoding="utf-8"
    )
    context = source.split("struct SnapshotTileEncodeAdmission {", 1)[1].split(
        "\n};", 1
    )[0]
    definitions = "struct SnapshotTileEncodeAdmission {" + context + "};\n"
    for signature in (
        "bool snapshot_cache_encode_jpeg(",
        "SnapshotPreviewReclaimStats snapshot_reclaim_idle_application_previews(",
        "bool snapshot_tile_cache_encode(",
    ):
        definition = production_function(source, signature)
        if signature.startswith("SnapshotPreviewReclaimStats"):
            definition = (
                "#ifdef USE_LVGL_SNAPSHOT_JPEG_CACHE\n" + definition + "#endif\n"
            )
        definitions += definition
    output = tmp_path_factory.mktemp("snapshot_jpeg_admission")
    (output / "snapshot_jpeg_admission_under_test.inc").write_text(
        definitions, encoding="utf-8"
    )
    executables = {}
    for mode in ("enabled", "disabled"):
        executable = output / (
            f"jpeg_admission_{mode}.exe"
            if os.name == "nt"
            else f"jpeg_admission_{mode}"
        )
        extra = (
            [
                "-DTEST_WITHOUT_JPEG_CACHE",
                "-Wno-unused-function",
                "-Wno-unused-parameter",
                "-Wno-unused-variable",
            ]
            if mode == "disabled"
            else []
        )
        compiled = subprocess.run(
            [
                *command,
                "-std=c++20",
                "-Wall",
                "-Wextra",
                "-Werror",
                *extra,
                "-I",
                str(output),
                "-I",
                str(root),
                str(Path(__file__).with_name("snapshot_jpeg_admission_test.cpp")),
                "-o",
                str(executable),
            ],
            capture_output=True,
            text=True,
            timeout=180,
            check=False,
        )
        assert compiled.returncode == 0, compiled.stdout + compiled.stderr
        executables[mode] = executable
    return executables


@pytest.mark.parametrize("case", range(18))
def test_snapshot_jpeg_admission(
    jpeg_admission_executables: dict[str, Path], case: int
) -> None:
    result = subprocess.run(
        [str(jpeg_admission_executables["enabled"]), str(case)],
        capture_output=True,
        text=True,
        timeout=15,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert f"case {case} PASS" in result.stdout


def test_snapshot_jpeg_admission_feature_disabled(
    jpeg_admission_executables: dict[str, Path],
) -> None:
    result = subprocess.run(
        [str(jpeg_admission_executables["disabled"]), "0"],
        capture_output=True,
        text=True,
        timeout=15,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "feature disabled PASS" in result.stdout


def test_admission_authority_is_stack_scoped_and_never_enters_workers() -> None:
    root = Path(__file__).resolve().parents[3]
    source = (root / "esphome/components/lvgl/lvgl_esphome.cpp").read_text(
        encoding="utf-8"
    )
    public = production_function(
        source, 'extern "C" bool lvgl_esphome_snapshot_cache_tile_window('
    )
    assert public.count("SnapshotTileEncodeAdmission encode_admission{") == 1
    assert source.count("SnapshotTileEncodeAdmission encode_admission{") == 1
    for signature in (
        "void snapshot_compression_worker_task(",
        "void snapshot_tile_prefetch_worker_task(",
        "bool snapshot_tile_prefetch_encode_outgoing(",
        'extern "C" bool lvgl_esphome_snapshot_refresh_tile_backing(',
    ):
        body = production_function(source, signature)
        assert "encode_admission" not in body
        assert "snapshot_reclaim_idle_application_previews" not in body
    for name in (
        "snapshot_tile_cache_encode",
        "snapshot_tile_window_flush_slot",
        "snapshot_tile_window_restore_slot",
        "snapshot_tile_window_restore_pending",
        "snapshot_tile_window_load_page",
    ):
        definition = production_function(source, "bool " + name + "(")
        assert "SnapshotTileEncodeAdmission *admission = nullptr" in definition
        for call in re.findall(name + r"\(([^;{]+?)\)", public):
            assert "&encode_admission" in call, (name, call)
