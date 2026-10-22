"""Execute real Lottie allocation/publication/retirement with hardware fakes."""

from __future__ import annotations

import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest


def _function(source: str, signature: str) -> str:
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


@pytest.fixture(scope="module")
def lifecycle_executable(tmp_path_factory: pytest.TempPathFactory) -> Path:
    if compiler := os.environ.get("ESPHOME_TEST_CXX"):
        command = [compiler, *shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", ""))]
    elif compiler := os.environ.get("CXX"):
        command = shlex.split(compiler, posix=os.name != "nt")
    elif compiler := (
        shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    ):
        command = [compiler]
    else:
        pytest.skip("A host C++ compiler is required for Lottie lifecycle tests")
    root = Path(__file__).resolve().parents[3]
    source = (root / "esphome/components/lvgl/lottie_loader.h").read_text(
        encoding="utf-8"
    )
    output = tmp_path_factory.mktemp("lottie_direct_opaque")
    constants_start = source.index("static constexpr size_t LOTTIE_TASK_STACK_SIZE")
    constants_end = source.index("struct LottieContext;", constants_start)
    struct_start = source.index("enum class LottiePublicationKind")
    struct_end = source.index("inline LottieContext *lottie_contexts", struct_start)
    claim_start = source.index("class LottieDirectRegionClaim")
    claim_end = source.index("inline bool lottie_apply_color_slot", claim_start)
    (output / "lottie_types.inc").write_text(
        source[constants_start:constants_end]
        + source[struct_start:struct_end]
        + source[claim_start:claim_end],
        encoding="utf-8",
    )
    signatures = (
        "inline lv_color_format_t lottie_argb_color_format(",
        "inline size_t lottie_buffer_bytes(",
        "inline bool lottie_uses_scaled_render(",
        "inline bool lottie_uses_direct_xrgb(",
        "inline bool lottie_uses_direct_scaled_argb(",
        "inline bool lottie_uses_direct_opaque_pipeline(",
        "inline size_t lottie_display_buffer_bytes(",
        "inline bool lottie_renders_to_display_back_buffer(",
        "inline uint8_t *lottie_render_target(",
        "inline uint8_t *lottie_initial_render_target(",
        "inline uint8_t *lottie_alloc_pixel_buffer(",
    )
    (output / "lottie_helpers.inc").write_text(
        "\n".join(_function(source, signature) for signature in signatures),
        encoding="utf-8",
    )
    for name, signature in (
        ("publication", "inline bool lottie_publish_direct_opaque_frame("),
        ("retention", "inline bool lottie_commit_direct_frame_to_canvas("),
        ("cleanup", "inline bool lottie_free_resources("),
    ):
        (output / f"lottie_{name}.inc").write_text(
            _function(source, signature), encoding="utf-8"
        )
    launch = source.index("inline bool lottie_launch(")
    start = source.index("  const size_t argb_bytes =", launch)
    end = source.index("  ctx->thorvg_target = nullptr;", start)
    (output / "lottie_allocation.inc").write_text(source[start:end], encoding="utf-8")
    worker = source.index("inline void lottie_render_task(")
    start = source.index("  uint8_t *first_target =", worker)
    end = source.index("\n  if (ctx->prepared_frame_ready", start)
    (output / "lottie_initial_prepare.inc").write_text(
        source[start:end], encoding="utf-8"
    )
    start = source.index(
        "    if (ctx->runtime_hidden && ctx->direct_region_active", worker
    )
    end = source.index("\n\n    if (!ctx->auto_start", start)
    (output / "lottie_hidden_drain.inc").write_text(source[start:end], encoding="utf-8")
    start = source.index("      if (color_applied && completed", worker)
    end = source.index("\n      }\n    }\n\n    if (ctx->restart_requested)", start)
    (output / "lottie_retained_recolor.inc").write_text(
        source[start:end] + "\n      }\n", encoding="utf-8"
    )
    executable = output / ("lifecycle.exe" if os.name == "nt" else "lifecycle")
    compiled = subprocess.run(
        [
            *command,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-const-variable",
            "-I",
            str(output),
            str(Path(__file__).with_name("lottie_direct_opaque_lifecycle_test.cpp")),
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


@pytest.mark.parametrize("case", range(16))
def test_actual_lottie_lifecycle(lifecycle_executable: Path, case: int) -> None:
    result = subprocess.run(
        [str(lifecycle_executable), str(case)],
        capture_output=True,
        text=True,
        timeout=15,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert f"case {case} PASS" in result.stdout
