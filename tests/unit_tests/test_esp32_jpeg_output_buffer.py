"""Run the production JPEG scratch planner in a small native C++ executable."""

import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest

REPO = Path(__file__).resolve().parents[2]
SOURCE = Path(__file__).parent / "fixtures/esp32_jpeg/encoder_output_buffer.cpp"


@pytest.fixture(scope="module")
def planner_executable(tmp_path_factory: pytest.TempPathFactory) -> Path:
    compiler = os.environ.get("ESPHOME_TEST_CXX") or shutil.which("c++")
    if compiler is None:
        pytest.skip("A native C++ compiler is required for the JPEG allocation planner")
    extra_args = shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", ""))
    output = tmp_path_factory.mktemp("jpeg_output_buffer") / (
        "planner.exe" if os.name == "nt" else "planner"
    )
    subprocess.run(
        [
            compiler,
            *extra_args,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(REPO),
            str(SOURCE),
            "-o",
            str(output),
        ],
        check=True,
        capture_output=True,
        text=True,
        timeout=120,
    )
    return output


@pytest.mark.parametrize("case", range(20))
def test_native_encoder_output_buffer(planner_executable: Path, case: int) -> None:
    subprocess.run(
        [str(planner_executable), str(case)],
        check=True,
        capture_output=True,
        text=True,
        timeout=10,
    )
