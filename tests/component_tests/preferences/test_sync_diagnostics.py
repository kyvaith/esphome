"""Validate opt-in codegen and execute the actual ESP32 sync with NVS fakes."""

from __future__ import annotations

from collections.abc import Callable
import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest

from esphome import config_validation as cv
from esphome.components import preferences
from esphome.const import PlatformFramework
from esphome.core import CORE
from tests.component_tests.types import SetCoreConfigCallable


@pytest.mark.parametrize("platform", list(PlatformFramework))
@pytest.mark.parametrize("option", ({}, {"sync_diagnostics": False}))
def test_disabled_sync_diagnostics_accepts_all_platforms(
    set_core_config: SetCoreConfigCallable,
    platform: PlatformFramework,
    option: dict,
) -> None:
    set_core_config(platform)
    assert preferences.CONFIG_SCHEMA(option)["sync_diagnostics"] is False


@pytest.mark.parametrize("platform", list(PlatformFramework))
def test_enabled_sync_diagnostics_requires_esp32(
    set_core_config: SetCoreConfigCallable, platform: PlatformFramework
) -> None:
    set_core_config(platform)
    if platform in (
        PlatformFramework.ESP32_IDF,
        PlatformFramework.ESP32_ARDUINO,
    ):
        assert (
            preferences.CONFIG_SCHEMA({"sync_diagnostics": True})["sync_diagnostics"]
            is True
        )
    else:
        with pytest.raises(cv.Invalid, match="only available on"):
            preferences.CONFIG_SCHEMA({"sync_diagnostics": True})


@pytest.mark.parametrize(
    ("target", "option", "enabled"),
    [
        ("host: {}", "", False),
        ("esp8266:\n  board: d1_mini", "sync_diagnostics: false", False),
        ("esp32:\n  board: esp32dev", "", False),
        ("esp32:\n  board: esp32dev", "sync_diagnostics: true", True),
        (
            "esp32:\n  board: esp32dev\n  framework:\n    type: arduino",
            "sync_diagnostics: true",
            True,
        ),
    ],
)
def test_sync_diagnostics_generated_define(
    generate_main: Callable[[str | Path], str],
    tmp_path: Path,
    target: str,
    option: str,
    enabled: bool,
) -> None:
    config = tmp_path / "sync.yaml"
    config.write_text(
        f"esphome:\n  name: pref-sync-test\n{target}\npreferences:\n  {option}\n",
        encoding="utf-8",
    )
    main = generate_main(config)
    assert (
        "USE_ESP32_PREFERENCES_SYNC_DIAGNOSTICS"
        in {define.name for define in CORE.defines}
    ) is enabled
    assert "set_update_interval(60000)" in main


@pytest.fixture(scope="module", params=(False, True), ids=("disabled", "enabled"))
def sync_executable(
    request: pytest.FixtureRequest, tmp_path_factory: pytest.TempPathFactory
) -> Path:
    if compiler := os.environ.get("ESPHOME_TEST_CXX"):
        command = [compiler, *shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", ""))]
    elif compiler := os.environ.get("CXX"):
        command = shlex.split(compiler, posix=os.name != "nt")
    elif compiler := (
        shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    ):
        command = [compiler]
    else:
        pytest.skip("A host C++ compiler is required for preferences sync tests")
    root = Path(__file__).resolve().parents[3]
    source = (root / "esphome/components/esp32/preferences.cpp").read_text(
        encoding="utf-8"
    )
    start = source.index("bool ESP32Preferences::sync() {")
    end = source.index("\nbool ESP32Preferences::is_changed_", start)
    output = tmp_path_factory.mktemp("preferences_sync")
    (output / "preferences_sync_under_test.inc").write_text(
        source[start:end], encoding="utf-8"
    )
    executable = output / ("sync.exe" if os.name == "nt" else "sync")
    compiled = subprocess.run(
        [
            *command,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-DTEST_DIAGNOSTICS={int(request.param)}",
            "-I",
            str(output),
            str(Path(__file__).with_name("sync_diagnostics_test.cpp")),
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


@pytest.mark.parametrize("case", range(8))
def test_actual_sync_diagnostics(sync_executable: Path, case: int) -> None:
    result = subprocess.run(
        [str(sync_executable), str(case)],
        capture_output=True,
        text=True,
        timeout=15,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert f"case {case} PASS" in result.stdout
