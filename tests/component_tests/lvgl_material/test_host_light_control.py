"""Dependency preflight checks do not invoke a compiler or fetch dependencies."""

from pathlib import Path

import host_light_control as dependencies
import pytest


def test_checkout_root_matches_component_test_placement():
    assert (
        dependencies.FORK / "tests/component_tests/lvgl_material" == dependencies.HERE
    )


def test_explicit_compiler_arguments_preserve_zig_subcommand(monkeypatch, tmp_path):
    compiler = tmp_path / "compiler with spaces"
    compiler.touch()
    monkeypatch.setenv("ESPHOME_TEST_CXX", str(compiler))
    monkeypatch.setenv("ESPHOME_TEST_CXX_ARGS", "c++ -target x86_64-windows-gnu")
    assert dependencies.compiler_command() == [
        str(compiler),
        "c++",
        "-target",
        "x86_64-windows-gnu",
    ]


def test_invalid_explicit_arduinojson_fails_before_compile(monkeypatch, tmp_path):
    monkeypatch.setenv("ESPHOME_TEST_ARDUINOJSON", str(tmp_path))
    with pytest.raises(pytest.fail.Exception, match="ArduinoJson.h"):
        dependencies.arduino_json()


def test_public_arduinojson7_headers_are_required(monkeypatch, tmp_path):
    (tmp_path / "ArduinoJson.h").write_text("// public header", encoding="utf-8")
    (tmp_path / "ArduinoJson").mkdir()
    version = tmp_path / "ArduinoJson/version.hpp"
    monkeypatch.setenv("ESPHOME_TEST_ARDUINOJSON", str(tmp_path))
    version.write_text("#define ARDUINOJSON_VERSION_MAJOR 6", encoding="utf-8")
    with pytest.raises(pytest.fail.Exception, match="ArduinoJson 7"):
        dependencies.arduino_json()
    version.write_text("#define ARDUINOJSON_VERSION_MAJOR 7", encoding="utf-8")
    assert dependencies.arduino_json() == Path(tmp_path).resolve()
