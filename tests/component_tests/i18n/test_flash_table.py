"""Immutable ESP32 translation tables retain legacy lookup semantics."""

import asyncio
import json
from pathlib import Path
import subprocess

import pytest
from test_capacity import AUTOMATION_STUB, LOG_STUB, compiler_command

from esphome import codegen as cg, config_validation as cv
from esphome.components import i18n
from esphome.const import KEY_CORE, KEY_TARGET_PLATFORM
from esphome.core import CORE


def flash_config(packs, platform="esp32"):
    CORE.data.setdefault(KEY_CORE, {})[KEY_TARGET_PLATFORM] = platform
    return i18n.CONFIG_SCHEMA(
        {"id": "translations", "translations": packs, "use_flash_table": True}
    )


@pytest.mark.parametrize("platform", ["host", "esp8266", "rp2040"])
def test_flash_table_is_esp32_only(platform):
    with pytest.raises(cv.Invalid, match="ESP32"):
        flash_config([{"locale": "en-US", "values": {}}], platform)


@pytest.mark.parametrize("empty", [False, True])
def test_flash_codegen_static_lifetime_and_order(monkeypatch, empty):
    async def register_component(*_):
        pass

    monkeypatch.setattr(cg, "register_component", register_component)
    packs = [
        {"locale": "en-US", "values": {} if empty else {"key": value}}
        for value in ("first", "second")
    ]
    CORE.config = {}
    asyncio.run(i18n.to_code(flash_config(packs)))
    CORE.flush_tasks()
    generated = str(CORE.cpp_main_section)
    globals_ = str(CORE.cpp_global_section)
    assert "->add_translation(" not in generated
    assert any(item.name == "USE_I18N_FLASH_TABLE" for item in CORE.defines)
    if empty:
        assert "->set_translation_table(nullptr, 0);" in generated
        assert "i18n_translations" not in globals_
    else:
        assert "static constexpr i18n::I18nComponent::Translation" in globals_
        assert "PROGMEM" in globals_
        assert globals_.index('"first"') < globals_.index('"second"')
        assert "->set_translation_table(i18n_translations, 2);" in generated


def run_lookup_table(tmp_path, entries, flash):
    """Run both real C++ storage modes with identical caller-owned literals."""
    tmp_path.mkdir(parents=True)
    stub = tmp_path / "esphome/core"
    stub.mkdir(parents=True)
    (stub / "defines.h").write_text("#pragma once\n", encoding="utf-8")
    (stub / "component.h").write_text(
        "#pragma once\nnamespace esphome { class Component {}; }\n", encoding="utf-8"
    )
    (stub / "automation.h").write_text(AUTOMATION_STUB, encoding="utf-8")
    (stub / "log.h").write_text(LOG_STUB, encoding="utf-8")
    source = Path(__file__).resolve().parents[3] / "esphome/components/i18n"
    rows = [
        "{"
        + ",".join(
            "nullptr" if value is None else json.dumps(value, ensure_ascii=False)
            for value in entry
        )
        + "}"
        for entry in entries
    ]
    # One sentinel storage row is valid C++ for an empty configured table, but
    # the bound count remains zero and it is never queried.
    table = ",\n".join(rows) if rows else "{nullptr, nullptr, nullptr}"
    locales = sorted(
        {entry[0] for entry in entries if entry[0] is not None} | {"fr-FR", "en-US"}
    )
    harness = tmp_path / "main.cpp"
    harness.write_text(
        HARNESS.replace("@TABLE@", table)
        .replace("@COUNT@", str(len(entries)))
        .replace("@LOCALES@", ",".join(json.dumps(locale) for locale in locales)),
        encoding="utf-8",
    )
    executable = tmp_path / "i18n_test.exe"
    command = [
        *compiler_command(),
        "-std=c++20",
        "-Wall",
        "-Wextra",
        "-Werror",
        f"-DI18N_TRANSLATION_CAPACITY={max(1, len(entries))}",
        *(["-DUSE_I18N_FLASH_TABLE"] if flash else []),
        "-I",
        str(tmp_path),
        "-I",
        str(source),
        str(source / "i18n.cpp"),
        str(harness),
        "-o",
        str(executable),
    ]
    result = subprocess.run(command, capture_output=True, timeout=120, check=False)
    assert result.returncode == 0, result.stdout + result.stderr
    result = subprocess.run(
        [str(executable)], capture_output=True, timeout=30, check=False
    )
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


@pytest.mark.parametrize("empty", [False, True])
def test_real_cpp_legacy_equivalence(tmp_path, empty):
    entries = (
        []
        if empty
        else [
            ("en-US", "key", "first"),
            ("en-US", "key", "second"),
            ("pl-PL", "key", "pierwszy"),
            ("pl-PL", "key", "drugi"),
            ("en-US", "fallback", "fallback value"),
            ("en-US", "empty", ""),
            (None, "invalid1", "ignored"),
            ("en-US", None, "ignored"),
            ("en-US", "invalid3", None),
        ]
    )
    legacy = run_lookup_table(tmp_path / "legacy", entries, False)
    flash = run_lookup_table(tmp_path / "flash", entries, True)
    assert flash == legacy
    if not empty:
        lines = flash.decode().splitlines()
        assert lines[0] == lines[2] == "first"
        assert lines[2 * len(entries)] == "first"  # Unsupported locale falls back.
        assert lines[4 * len(entries)] == "pierwszy"
        assert lines[4 * len(entries) + 8] == "fallback value"


HARNESS = r"""
#include "i18n.h"
#include <cassert>
#include <iostream>
#include <string>
unsigned i18n_test_errors = 0;
using esphome::i18n::I18nComponent;
static constexpr I18nComponent::Translation TABLE[] = {@TABLE@};
static constexpr size_t COUNT = @COUNT@;
int main() {
  I18nComponent translations;
  translations.set_default_locale(nullptr);
  assert(std::string(translations.translate(nullptr)).empty());
  assert(std::string(translations.translate_existing(nullptr)).empty());
  assert(std::string(translations.translate_existing("")).empty());
#ifdef USE_I18N_FLASH_TABLE
  translations.set_translation_table(nullptr, 0);
  translations.set_translation_table(TABLE, COUNT);
  translations.set_translation_table(nullptr, 1);
  translations.set_translation_table(TABLE, I18nComponent::MAX_TRANSLATIONS + 1);
  assert(i18n_test_errors == 2);
  translations.add_translation("en-US", "manual", "must not be added");
  assert(i18n_test_errors == 3);
  assert(std::string(translations.translate("manual")) == "manual");
#else
  for (size_t i = 0; i < COUNT; ++i) {
    const auto &entry = TABLE[i];
    translations.add_translation(entry.locale, entry.key, entry.value);
  }
  assert(i18n_test_errors == 0);
#endif
  for (const char *locale : {@LOCALES@}) {
    assert(translations.set_locale(locale));
    for (size_t i = 0; i < COUNT; ++i) {
      const auto &entry = TABLE[i];
      std::cout << translations.translate(entry.key) << '\n';
      std::cout << translations.translate_existing(entry.value) << '\n';
    }
    assert(std::string(translations.translate("unregistered key")) == "unregistered key");
    assert(std::string(translations.translate_existing("Home Assistant title")) == "Home Assistant title");
    assert(!translations.set_locale(""));
    assert(!translations.set_locale("overlong-locale"));
    assert(std::string(translations.current_locale()) == locale);
  }
#ifdef USE_I18N_FLASH_TABLE
  translations.set_translation_table(nullptr, 0);
  assert(std::string(translations.translate("key")) == "key");
#endif
}
"""
