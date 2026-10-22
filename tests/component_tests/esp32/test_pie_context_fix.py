"""Keep the reviewed P4 PIE backport explicit and native ESP-IDF only."""

from collections.abc import Callable
from pathlib import Path

import pytest

from esphome import config_validation as cv
from esphome.components.esp32 import CONFIG_SCHEMA
from esphome.components.esp32.const import KEY_COMPONENTS, KEY_SDKCONFIG_OPTIONS
from esphome.const import KEY_ESP32, PlatformFramework, Toolchain
from esphome.core import CORE
from tests.component_tests.types import SetCoreConfigCallable


@pytest.mark.parametrize(
    ("variant", "version", "toolchain", "enabled", "accepted"),
    [
        ("ESP32P4", "5.5.5", Toolchain.ESP_IDF, True, True),
        ("ESP32P4", "5.5.6", Toolchain.ESP_IDF, True, True),
        ("ESP32P4", "5.5.7", Toolchain.ESP_IDF, True, False),
        ("ESP32P4", "5.5.4", Toolchain.ESP_IDF, True, False),
        ("ESP32P4", "5.5.6-dev", Toolchain.ESP_IDF, True, False),
        ("ESP32S3", "5.5.6", Toolchain.ESP_IDF, True, False),
        ("ESP32P4", "5.5.6", Toolchain.PLATFORMIO, True, False),
        ("ESP32P4", "5.5.7", Toolchain.ESP_IDF, False, True),
        ("ESP32S3", "5.5.6", Toolchain.ESP_IDF, False, True),
        ("ESP32P4", "5.5.6", Toolchain.PLATFORMIO, False, True),
    ],
)
def test_pie_context_fix_scope(
    set_core_config: SetCoreConfigCallable,
    variant: str,
    version: str,
    toolchain: Toolchain,
    enabled: bool,
    accepted: bool,
) -> None:
    """Enabling the fix requires every reviewed target, SDK and build condition."""
    set_core_config(PlatformFramework.ESP32_IDF)
    config = {
        "variant": variant,
        "toolchain": toolchain.value,
        "framework": {
            "type": "esp-idf",
            "version": version,
            "advanced": {"enable_pie_context_fix": enabled},
        },
    }
    if accepted:
        assert (
            CONFIG_SCHEMA(config)["framework"]["advanced"]["enable_pie_context_fix"]
            is enabled
        )
    else:
        with pytest.raises(cv.Invalid, match="enable_pie_context_fix requires"):
            CONFIG_SCHEMA(config)


@pytest.mark.parametrize("enabled", [None, False])
def test_pie_context_fix_default_is_disabled(
    set_core_config: SetCoreConfigCallable, enabled: bool | None
) -> None:
    """Existing configurations do not opt in to a framework source backport."""
    set_core_config(PlatformFramework.ESP32_IDF)
    advanced = {} if enabled is None else {"enable_pie_context_fix": enabled}
    config = CONFIG_SCHEMA(
        {
            "variant": "ESP32P4",
            "framework": {
                "type": "esp-idf",
                "version": "5.5.6",
                "advanced": advanced,
            },
        }
    )
    assert config["framework"]["advanced"]["enable_pie_context_fix"] is False


def test_pie_context_fix_rejects_arduino(
    set_core_config: SetCoreConfigCallable,
) -> None:
    """Arduino's bundled IDF does not opt in to native SDK patching."""
    set_core_config(PlatformFramework.ESP32_ARDUINO)
    with pytest.raises(cv.Invalid, match="enable_pie_context_fix requires"):
        CONFIG_SCHEMA(
            {
                "variant": "ESP32S3",
                "framework": {
                    "type": "arduino",
                    "advanced": {"enable_pie_context_fix": True},
                },
            }
        )


@pytest.mark.parametrize("version", ["5.5.5", "5.5.6"])
@pytest.mark.parametrize("enabled", [None, False, True])
def test_pie_context_fix_codegen(
    tmp_path: Path,
    generate_main: Callable[[str | Path], str],
    version: str,
    enabled: bool | None,
) -> None:
    """Only opt-in codegen registers the native patch component and Kconfig flag."""
    source = tmp_path / "pie-context.yaml"
    advanced = (
        ""
        if enabled is None
        else f"    advanced:\n      enable_pie_context_fix: {str(enabled).lower()}\n"
    )
    source.write_text(
        "esphome:\n  name: pie-context-test\n"
        "esp32:\n  variant: esp32p4\n  toolchain: esp-idf\n"
        f"  framework:\n    type: esp-idf\n    version: {version}\n{advanced}",
        encoding="utf-8",
    )
    generate_main(source)
    components = CORE.data[KEY_ESP32][KEY_COMPONENTS]
    sdkconfig = CORE.data[KEY_ESP32][KEY_SDKCONFIG_OPTIONS]
    if enabled:
        assert components["pie_context_framework_patch"] == {
            "repo": None,
            "ref": None,
            "path": (
                Path(__file__).resolve().parents[3]
                / "esphome/components/esp32/pie_context_framework_patch"
            ).as_posix(),
        }
        assert sdkconfig["CONFIG_ESPHOME_ESP32P4_PIE_CONTEXT_FIX"] is True
    else:
        assert "pie_context_framework_patch" not in components
        assert "CONFIG_ESPHOME_ESP32P4_PIE_CONTEXT_FIX" not in sdkconfig
    assert "cache_restart_framework_patch" not in components
    assert "CONFIG_ESPHOME_ESP32P4_CACHE_SAFE_RESTART" not in sdkconfig
