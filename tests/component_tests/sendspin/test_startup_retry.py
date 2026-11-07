"""Exercise actual Hub startup methods and bounded retry state, without device I/O."""

import ast
from collections.abc import Generator
import os
from pathlib import Path
import shlex
import shutil
import subprocess
from types import SimpleNamespace
from unittest.mock import Mock

import pytest

from esphome import config_validation as cv
from esphome.components import sendspin
from esphome.components.sendspin import CONFIG_SCHEMA
from esphome.const import KEY_CORE, KEY_TARGET_FRAMEWORK, KEY_TARGET_PLATFORM, Toolchain
from esphome.core import CORE

ROOT = Path(__file__).resolve().parents[3]
COMPONENT = ROOT / "esphome/components/sendspin"


def _method(source: str, name: str) -> str:
    signature = f"void SendspinHub::{name}("
    assert source.count(signature) == 1
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        assert end < len(source)
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + "\n"


@pytest.fixture(scope="module", params=[0, 1])
def startup_executable(
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
        pytest.skip("A host C++ compiler is required for Sendspin startup tests")
    output = tmp_path_factory.mktemp(f"sendspin_startup_{request.param}")
    source = (COMPONENT / "sendspin_hub.cpp").read_text(encoding="utf-8")
    (output / "actual_hub_methods.inc").write_text(
        "\n".join(
            _method(source, name) for name in ("loop", "set_enabled", "on_shutdown")
        ),
        encoding="utf-8",
    )
    executable = output / ("startup.exe" if os.name == "nt" else "startup")
    result = subprocess.run(
        [
            *command,
            "-std=c++20",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-UNDEBUG",
            f"-DCONFIG_ESPHOME_SENDSPIN_CHECKED_THREADS={request.param}",
            "-I",
            str(ROOT),
            "-I",
            str(output),
            str(Path(__file__).with_name("startup_retry_test.cpp")),
            "-o",
            str(executable),
        ],
        capture_output=True,
        text=True,
        timeout=180,
        check=False,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    return executable, request.param


def test_actual_hub_startup(startup_executable: tuple[Path, int]) -> None:
    executable, checked = startup_executable
    for case in range(15 if checked else 6):
        result = subprocess.run(
            [str(executable), str(case)],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
        assert result.returncode == 0, f"case {case}: {result.stdout}{result.stderr}"
        assert f"checked {checked} case {case} PASS" in result.stdout
        print(result.stdout.rstrip())


@pytest.fixture
def native_core() -> Generator[None]:
    CORE.reset()
    CORE.data[KEY_CORE] = {
        KEY_TARGET_PLATFORM: "esp32",
        KEY_TARGET_FRAMEWORK: "esp-idf",
    }
    CORE.toolchain = Toolchain.ESP_IDF
    yield
    CORE.reset()


def test_retry_schema_defaults_off(native_core: None) -> None:
    config = CONFIG_SCHEMA({"id": "hub"})
    assert config["startup_retries"] == 0
    assert config["optimize_memory"] is False


def test_retry_schema_is_independent_of_memory_optimization(native_core: None) -> None:
    config = CONFIG_SCHEMA(
        {"id": "hub", "startup_retries": 4, "optimize_memory": False}
    )
    assert config["startup_retries"] == 4
    assert config["optimize_memory"] is False


@pytest.mark.parametrize("value", [-1, 5, 255])
def test_retry_schema_rejects_outside_budget(native_core: None, value: int) -> None:
    with pytest.raises(cv.Invalid):
        CONFIG_SCHEMA({"id": "hub", "startup_retries": value})


def test_retry_schema_rejects_non_native(native_core: None) -> None:
    CORE.toolchain = Toolchain.PLATFORMIO
    with pytest.raises(cv.Invalid, match="startup_retries requires the native ESP-IDF"):
        CONFIG_SCHEMA({"id": "hub", "startup_retries": 4})
    assert CONFIG_SCHEMA({"id": "hub"})["startup_retries"] == 0


@pytest.mark.parametrize("native,retries", [(True, 4), (True, 0), (False, 0)])
def test_actual_codegen_registers_checked_threads_without_memory_optimization(
    native_core: None, native: bool, retries: int
) -> None:
    """Execute actual dependency/setter statements; replace only codegen sinks."""
    CORE.toolchain = Toolchain.ESP_IDF if native else Toolchain.PLATFORMIO
    config = CONFIG_SCHEMA(
        {"id": "hub", "startup_retries": retries, "optimize_memory": False}
    )
    tree = ast.parse((COMPONENT / "__init__.py").read_text(encoding="utf-8"))
    to_code = next(
        node
        for node in tree.body
        if isinstance(node, ast.AsyncFunctionDef) and node.name == "to_code"
    )
    owned_literals = {
        "sendspin/sendspin-cpp",
        "CONFIG_ESPHOME_SENDSPIN_CHECKED_THREADS",
        "CONFIG_ESPHOME_SENDSPIN_MEMORY_OPTIMIZATIONS",
    }
    statements = [
        statement
        for statement in to_code.body
        if any(
            (
                isinstance(node, ast.Constant)
                and isinstance(node.value, str)
                and node.value in owned_literals
            )
            or (
                isinstance(node, ast.Call)
                and isinstance(node.func, ast.Attribute)
                and node.func.attr == "set_startup_retries"
            )
            for node in ast.walk(statement)
        )
    ]
    assert (
        len(statements) == 4
    )  # Setter, dependency, checked-thread hook, optional memory guard.
    variable = SimpleNamespace(set_startup_retries=Mock(return_value="retry-setter"))
    codegen = SimpleNamespace(add=Mock())
    platform = SimpleNamespace(
        add_idf_component=Mock(), add_idf_sdkconfig_option=Mock()
    )
    namespace = dict(vars(sendspin))
    namespace.update(config=config, var=variable, cg=codegen, esp32=platform)
    actual = ast.fix_missing_locations(ast.Module(body=statements, type_ignores=[]))
    exec(compile(actual, str(COMPONENT / "__init__.py"), "exec"), namespace)
    if retries:
        variable.set_startup_retries.assert_called_once_with(retries)
        codegen.add.assert_called_once_with("retry-setter")
    else:
        variable.set_startup_retries.assert_not_called()
        codegen.add.assert_not_called()
    calls = platform.add_idf_component.call_args_list
    assert calls[0].kwargs == {"name": "sendspin/sendspin-cpp", "ref": "0.9.3"}
    if native:
        assert len(calls) == 2
        assert calls[1].kwargs == {
            "name": "sendspin_memory_patch",
            "path": (COMPONENT / "sendspin_memory_patch").as_posix(),
        }
        platform.add_idf_sdkconfig_option.assert_called_once_with(
            "CONFIG_ESPHOME_SENDSPIN_CHECKED_THREADS", True
        )
    else:
        assert len(calls) == 1
        platform.add_idf_sdkconfig_option.assert_not_called()
