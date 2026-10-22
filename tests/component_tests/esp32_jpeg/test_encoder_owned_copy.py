"""Execute the production JPEG publication branch with deterministic heap fakes."""

from __future__ import annotations

import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest


@pytest.fixture(scope="module", params=(False, True), ids=("psram", "psram-no-enc"))
def publication_executable(
    request: pytest.FixtureRequest, tmp_path_factory: pytest.TempPathFactory
) -> Path:
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
        pytest.skip(
            "A host C++ compiler is required for JPEG publication ownership tests"
        )
    root = Path(__file__).resolve().parents[3]
    source = (root / "esphome/components/esp32_jpeg/esp32_jpeg.cpp").read_text(
        encoding="utf-8"
    )
    encode_start = source.index("esp_err_t encode(const EncodeConfig &config,")
    start = source.index("  uint8_t *stored_data =", encode_start)
    end = source.index("\n#else\n  return ESP_ERR_NOT_SUPPORTED;", start)
    output = tmp_path_factory.mktemp("encoder_owned_copy")
    (output / "encoder_publication_under_test.inc").write_text(
        source[start:end], encoding="utf-8"
    )
    executable = output / ("publication.exe" if os.name == "nt" else "publication")
    compiled = subprocess.run(
        [
            *command,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-DTEST_NO_ENC={int(request.param)}",
            "-I",
            str(output),
            str(Path(__file__).with_name("encoder_owned_copy_test.cpp")),
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


@pytest.mark.parametrize("case", range(10))
def test_encoder_owned_copy(publication_executable: Path, case: int) -> None:
    result = subprocess.run(
        [str(publication_executable), str(case)],
        capture_output=True,
        text=True,
        timeout=15,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert f"case {case} PASS" in result.stdout
