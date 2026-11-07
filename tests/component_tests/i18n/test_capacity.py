"""Configured translation capacity and real C++ lookup beyond the old 512 limit."""

import asyncio
import os
from pathlib import Path
import shlex
import shutil
import subprocess

import pytest

from esphome import codegen as cg, config_validation as cv
from esphome.components import i18n
from esphome.core import CORE


def packs():
    result = []
    for locale, title, ready in (
        ("en-US", "Humidifier", "Choose a control"),
        ("pl-PL", "Nawilżacz", "Wybierz ustawienie"),
    ):
        values = {f"key.{index}": f"value.{index}" for index in range(269)}
        values["humidifier_control.humidifier"] = title
        values["device_control.ready"] = ready
        result.append({"locale": locale, "values": values})
    return result


def config(translations):
    return i18n.CONFIG_SCHEMA({"id": "translations", "translations": translations})


def generate(monkeypatch, translations):
    async def register_component(*_):
        pass

    monkeypatch.setattr(cg, "register_component", register_component)
    asyncio.run(i18n.to_code(config(translations)))
    CORE.flush_tasks()
    definition = next(
        item for item in CORE.defines if item.name == "I18N_TRANSLATION_CAPACITY"
    )
    return definition.as_macro, str(CORE.cpp_main_section)


def test_codegen_retains_entries_after_old_capacity(monkeypatch):
    definition, generated = generate(monkeypatch, packs())
    assert definition == "#define I18N_TRANSLATION_CAPACITY 542"
    assert generated.count("->add_translation(") == 542
    assert '"pl-PL", "humidifier_control.humidifier", "Nawilżacz"' in generated
    assert '"pl-PL", "device_control.ready", "Wybierz ustawienie"' in generated


def test_empty_pack_uses_one_storage_slot(monkeypatch):
    definition, generated = generate(monkeypatch, [{"locale": "en-US", "values": {}}])
    assert definition == "#define I18N_TRANSLATION_CAPACITY 1"
    assert "->add_translation(" not in generated


def test_repeated_locale_pack_keeps_first_match_order(monkeypatch):
    definition, generated = generate(
        monkeypatch,
        [
            {"locale": "en-US", "values": {"key": value}}
            for value in ("first", "second")
        ],
    )
    assert definition == "#define I18N_TRANSLATION_CAPACITY 2"
    assert generated.index('"key", "first"') < generated.index('"key", "second"')


def test_counter_bound_includes_every_pack():
    values = {str(index): "value" for index in range(65535)}
    assert (
        i18n.translation_count(config([{"locale": "en-US", "values": values}])) == 65535
    )
    with pytest.raises(cv.Invalid, match="65535"):
        config(
            [
                {"locale": "en-US", "values": values},
                {"locale": "en-US", "values": {"0": "duplicate still consumes a slot"}},
            ]
        )


def compiler_command():
    if compiler := os.environ.get("ESPHOME_TEST_CXX"):
        if not Path(compiler).is_file() and shutil.which(compiler) is None:
            pytest.fail("ESPHOME_TEST_CXX does not identify an available compiler")
        return [compiler, *shlex.split(os.environ.get("ESPHOME_TEST_CXX_ARGS", ""))]
    if (
        compiler := shutil.which("c++")
        or shutil.which("g++")
        or shutil.which("clang++")
    ):
        return [compiler]
    pytest.skip("A host C++20 compiler is required")


@pytest.mark.parametrize("capacity", [1, 542])
def test_runtime_lookup_and_explicit_overflow(tmp_path, capacity):
    """Compile the production component, stubbing only framework/log plumbing."""
    stub = tmp_path / "esphome/core"
    stub.mkdir(parents=True)
    (stub / "defines.h").write_text("#pragma once\n", encoding="utf-8")
    (stub / "component.h").write_text(
        "#pragma once\nnamespace esphome { class Component {}; }\n", encoding="utf-8"
    )
    (stub / "automation.h").write_text(AUTOMATION_STUB, encoding="utf-8")
    (stub / "log.h").write_text(LOG_STUB, encoding="utf-8")
    source = Path(__file__).resolve().parents[3] / "esphome/components/i18n"
    harness = tmp_path / "main.cpp"
    harness.write_text(HARNESS, encoding="utf-8")
    executable = tmp_path / "i18n_test.exe"
    command = [
        *compiler_command(),
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-DI18N_TRANSLATION_CAPACITY={capacity}",
        "-I",
        str(tmp_path),
        "-I",
        str(source),
        str(source / "i18n.cpp"),
        str(harness),
        "-o",
        str(executable),
    ]
    result = subprocess.run(
        command, capture_output=True, text=True, timeout=120, check=False
    )
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run(
        [str(executable)], capture_output=True, text=True, timeout=20, check=False
    )
    assert result.returncode == 0, result.stdout + result.stderr


AUTOMATION_STUB = r"""
#pragma once
namespace esphome {
template<typename T> struct TemplatableValue { template<typename... Ts> T value(Ts...) { return T{}; } };
template<typename... Ts> struct Action { virtual ~Action() = default; virtual void play(Ts...) = 0; };
}
#define TEMPLATABLE_VALUE(type, name) esphome::TemplatableValue<type> name##_;
"""

LOG_STUB = r"""
#pragma once
extern unsigned i18n_test_errors;
#define ESP_LOGI(tag, ...) ((void) (tag))
#define ESP_LOGE(tag, ...) do { (void) (tag); ++i18n_test_errors; } while (0)
"""

HARNESS = r"""
#include "i18n.h"
#include <cassert>
#include <string>
unsigned i18n_test_errors = 0;
int main() {
  using esphome::i18n::I18nComponent;
  I18nComponent translations;
  translations.set_default_locale("en-US");
  assert(std::string(translations.translate("missing")) == "missing");
  assert(std::string(translations.translate(nullptr)).empty());
  assert(std::string(translations.translate_existing(nullptr)).empty());
  translations.add_translation(nullptr, "invalid", "ignored");
  assert(i18n_test_errors == 0);
#if I18N_TRANSLATION_CAPACITY > 512
  std::array<std::string, 269> keys;
  for (size_t index = 0; index < keys.size(); index++) keys[index] = "key." + std::to_string(index);
  for (const char *locale : {"en-US", "pl-PL"}) {
    for (const auto &key : keys) translations.add_translation(locale, key.c_str(), "value");
    const bool polish = std::string(locale) == "pl-PL";
    translations.add_translation(locale, "humidifier_control.humidifier", polish ? "Nawilżacz" : "Humidifier");
    translations.add_translation(locale, "device_control.ready", polish ? "Wybierz ustawienie" : "Choose a control");
  }
  assert(translations.set_locale("pl-PL"));
  assert(std::string(translations.translate("humidifier_control.humidifier")) == "Nawilżacz");
  assert(std::string(translations.translate("device_control.ready")) == "Wybierz ustawienie");
  assert(std::string(translations.translate_existing("Humidifier")) == "Nawilżacz");
  assert(std::string(translations.translate_existing("Home Assistant title")) == "Home Assistant title");
  assert(!translations.set_locale(""));
  assert(!translations.set_locale("overlong-locale"));
  assert(std::string(translations.current_locale()) == "pl-PL");
  assert(translations.set_locale("fr-FR"));
  assert(std::string(translations.translate("device_control.ready")) == "Choose a control");
  assert(translations.set_locale("en-US"));
  assert(std::string(translations.translate("humidifier_control.humidifier")) == "Humidifier");
  I18nComponent duplicate;
  duplicate.add_translation("en-US", "key", "first");
  duplicate.add_translation("en-US", "key", "second");
  assert(std::string(duplicate.translate("key")) == "first");
#else
  translations.add_translation("en-US", "key", "first");
  assert(std::string(translations.translate("key")) == "first");
#endif
  assert(i18n_test_errors == 0);
  translations.add_translation("en-US", "overflow", "must not be stored");
  assert(i18n_test_errors == 1);
  assert(std::string(translations.translate("overflow")) == "overflow");
  return 0;
}
"""
