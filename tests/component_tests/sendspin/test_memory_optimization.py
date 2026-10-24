"""Execute the guarded 0.9.3 budget and assembly owners with a tracked host allocator."""

from collections.abc import Callable
import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest

from esphome import config_validation as cv
from esphome.components.esp32 import (
    KEY_COMPONENTS,
    KEY_ESP32,
    KEY_PATH,
    KEY_SDKCONFIG_OPTIONS,
)
from esphome.components.sendspin import CONFIG_SCHEMA
from esphome.const import PlatformFramework, Toolchain
from esphome.core import CORE
from tests.component_tests.types import SetCoreConfigCallable

ROOT = Path(__file__).resolve().parents[3]
PATCH = (
    ROOT / "esphome/components/sendspin/sendspin_memory_patch/patch_sendspin_memory.py"
)
SPEC = importlib.util.spec_from_file_location("sendspin_memory_patch", PATCH)
assert SPEC and SPEC.loader
PATCH_MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PATCH_MODULE)


def _patch_excerpt(source: str, owner: str) -> str:
    for old, new in PATCH_MODULE.replacements()[owner]:
        if old in source:
            source = PATCH_MODULE.transform_source(source, ((old, new),))
    return source


@pytest.fixture(scope="module", params=[0, 1])
def memory_executable(
    request: pytest.FixtureRequest, tmp_path_factory: pytest.TempPathFactory
) -> tuple[Path, int]:
    if compiler := os.environ.get("ESPHOME_TEST_CXX"):
        command = [compiler, *shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", ""))]
    elif (
        compiler := shutil.which("c++")
        or shutil.which("g++")
        or shutil.which("clang++")
    ):
        command = [compiler]
    else:
        pytest.skip(
            "A host C++ compiler is required for Sendspin memory lifecycle tests"
        )
    output = tmp_path_factory.mktemp(f"sendspin_memory_{request.param}")
    fixtures = Path(__file__).with_name("fixtures")
    for fixture, target, owner in (
        ("artwork_memory_093.cpp.inc", "artwork.inc", "src/artwork_role.cpp"),
        ("inbound_budget_093.cpp.inc", "budget.inc", "src/inbound_ring.h"),
    ):
        source = (fixtures / fixture).read_text(encoding="utf-8")
        source = _patch_excerpt(source, owner)
        if target == "budget.inc":
            # This helper is appended after the existing derivation by the source patch.
            source += PATCH_MODULE.guarded(PATCH_MODULE.CAPACITY_FLOOR)
        (output / target).write_text(source, encoding="utf-8")
    executable = output / ("memory.exe" if os.name == "nt" else "memory")
    compiled = subprocess.run(
        [
            *command,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-Wno-unused-const-variable",
            f"-DCONFIG_ESPHOME_SENDSPIN_MEMORY_OPTIMIZATIONS={request.param}",
            "-I",
            str(output),
            str(Path(__file__).with_name("memory_optimization_test.cpp")),
            "-o",
            str(executable),
        ],
        capture_output=True,
        text=True,
        timeout=180,
        check=False,
    )
    assert compiled.returncode == 0, compiled.stdout + compiled.stderr
    return executable, request.param


@pytest.mark.parametrize("case", range(14))
def test_actual_memory_owners(memory_executable: tuple[Path, int], case: int) -> None:
    executable, mode = memory_executable
    result = subprocess.run(
        [str(executable), str(case)],
        capture_output=True,
        text=True,
        timeout=15,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert f"mode {mode} case {case} PASS" in result.stdout


def test_memory_optimization_defaults_off(
    set_core_config: SetCoreConfigCallable,
) -> None:
    set_core_config(PlatformFramework.ESP32_IDF)
    assert CONFIG_SCHEMA({"id": "hub"})["optimize_memory"] is False


def test_codegen_registers_native_dependency_patch(
    generate_main: Callable[[str | Path], str],
    component_config_path: Callable[[str], Path],
) -> None:
    generate_main(component_config_path("memory_optimized_native.yaml"))
    esp32_data = CORE.data[KEY_ESP32]
    assert "sendspin_memory_patch" in esp32_data[KEY_COMPONENTS]
    native_path = Path(esp32_data[KEY_COMPONENTS]["sendspin_memory_patch"][KEY_PATH])
    for filename in ("CMakeLists.txt", "Kconfig.projbuild", "patch_sendspin_memory.py"):
        assert (native_path / filename).is_file()
    assert (
        esp32_data[KEY_SDKCONFIG_OPTIONS][
            "CONFIG_ESPHOME_SENDSPIN_MEMORY_OPTIMIZATIONS"
        ]
        is True
    )


def test_native_patch_inputs_survive_git_source_inventory(tmp_path: Path) -> None:
    """A native build must survive the same Git inventory used for source packaging."""
    if (git := shutil.which("git")) is None:
        pytest.skip("Git is required to verify source packaging")
    subprocess.run([git, "init", "--quiet", str(tmp_path)], check=True)
    shutil.copyfile(ROOT / ".gitignore", tmp_path / ".gitignore")
    native = Path("esphome/components/sendspin/sendspin_memory_patch")
    required = {
        native / name
        for name in ("CMakeLists.txt", "Kconfig.projbuild", "patch_sendspin_memory.py")
    }
    (tmp_path / native).mkdir(parents=True)
    for relative in required:
        shutil.copyfile(ROOT / relative, tmp_path / relative)
    inventory = subprocess.run(
        [
            git,
            "-C",
            str(tmp_path),
            "ls-files",
            "--modified",
            "--others",
            "--exclude-standard",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    assert {p.as_posix() for p in required}.issubset(inventory.stdout.splitlines())


def test_memory_optimization_requires_native_toolchain(
    set_core_config: SetCoreConfigCallable,
) -> None:
    set_core_config(PlatformFramework.ESP32_IDF)
    with pytest.raises(cv.Invalid, match="native ESP-IDF"):
        CONFIG_SCHEMA({"id": "hub", "optimize_memory": True})
    CORE.toolchain = Toolchain.ESP_IDF
    assert (
        CONFIG_SCHEMA({"id": "hub", "optimize_memory": True})["optimize_memory"] is True
    )


def test_patch_rejects_changed_or_duplicate_owner() -> None:
    with pytest.raises(RuntimeError, match="owner changed"):
        PATCH_MODULE.transform_source("changed", (("owner", "fixed"),))
    with pytest.raises(RuntimeError, match="owner changed"):
        PATCH_MODULE.transform_source("owner owner", (("owner", "fixed"),))


def test_patch_rejects_unknown_component_version(tmp_path: Path) -> None:
    (tmp_path / "idf_component.yml").write_text("version: 0.9.4\n", encoding="utf-8")
    with pytest.raises(RuntimeError, match="reviewed 0.9.3"):
        PATCH_MODULE.patch_component(tmp_path)


def test_patch_rejects_unreviewed_source_without_writes(tmp_path: Path) -> None:
    (tmp_path / "idf_component.yml").write_text("version: 0.9.3\n", encoding="utf-8")
    originals = {}
    for name in PATCH_MODULE.PRISTINE_SHA256:
        target = tmp_path / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(b"an unreviewed source\n")
        originals[target] = target.read_bytes()
    with pytest.raises(RuntimeError, match="Unreviewed Sendspin source"):
        PATCH_MODULE.patch_component(tmp_path)
    assert all(path.read_bytes() == content for path, content in originals.items())


def test_client_keeps_quotas_and_adds_capacity_floor() -> None:
    source = (
        Path(__file__).with_name("fixtures") / "client_budget_093.cpp.inc"
    ).read_text(encoding="utf-8")
    patched = _patch_excerpt(source, "src/client.cpp")
    assert (
        "derive_inbound_ring_bytes_preserving_audio_capacity(budget, player_capacity_share)"
        in patched
    )
    for holder, quota in (
        ("PLAYER", "audio"),
        ("ARTWORK", "artwork"),
        ("VISUALIZER", "visualizer"),
    ):
        assert (
            f"ring->quota(InboundHolder::{holder}).set_limit(budget.{quota}_hold_bytes);"
            in patched
        )
