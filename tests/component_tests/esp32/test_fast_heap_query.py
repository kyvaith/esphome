"""Keep the bounded heap query opt-in and limited to the reviewed native SDK."""

from collections.abc import Callable
from pathlib import Path

import pytest
from tests.component_tests.types import SetCoreConfigCallable

from esphome import config_validation as cv
from esphome.components import esp32
from esphome.components.esp32.const import KEY_COMPONENTS, KEY_SDKCONFIG_OPTIONS
from esphome.const import KEY_ESP32, PlatformFramework, Toolchain
from esphome.core import CORE


@pytest.mark.parametrize(
    ("variant", "version", "toolchain", "enabled", "accepted"),
    [
        ("ESP32P4", "5.5.6", Toolchain.ESP_IDF, True, True),
        ("ESP32P4", "5.5.5", Toolchain.ESP_IDF, True, False),
        ("ESP32P4", "5.5.7", Toolchain.ESP_IDF, True, False),
        ("ESP32S3", "5.5.6", Toolchain.ESP_IDF, True, False),
        ("ESP32P4", "5.5.6", Toolchain.PLATFORMIO, True, False),
        ("ESP32S3", "5.5.6", Toolchain.ESP_IDF, False, True),
        ("ESP32P4", "5.5.7", Toolchain.ESP_IDF, False, True),
        ("ESP32P4", "5.5.6", Toolchain.PLATFORMIO, False, True),
    ],
)
def test_fast_heap_query_scope(
    set_core_config: SetCoreConfigCallable,
    variant: str,
    version: str,
    toolchain: Toolchain,
    enabled: bool,
    accepted: bool,
) -> None:
    """Only opt-in configurations require the exact target, SDK and build path."""
    set_core_config(PlatformFramework.ESP32_IDF)
    config = {
        "variant": variant,
        "toolchain": toolchain.value,
        "framework": {
            "type": "esp-idf",
            "version": version,
            "advanced": {"enable_fast_heap_query": enabled},
        },
    }
    if accepted:
        assert (
            esp32.CONFIG_SCHEMA(config)["framework"]["advanced"][
                "enable_fast_heap_query"
            ]
            is enabled
        )
    else:
        with pytest.raises(cv.Invalid, match="enable_fast_heap_query requires"):
            esp32.CONFIG_SCHEMA(config)


@pytest.mark.parametrize("enabled", [None, False])
def test_fast_heap_query_default_is_disabled(
    set_core_config: SetCoreConfigCallable, enabled: bool | None
) -> None:
    """Existing configurations do not silently opt into a source backport."""
    set_core_config(PlatformFramework.ESP32_IDF)
    config = esp32.CONFIG_SCHEMA(
        {
            "variant": "ESP32P4",
            "framework": {
                "type": "esp-idf",
                "version": "5.5.6",
                "advanced": {}
                if enabled is None
                else {"enable_fast_heap_query": enabled},
            },
        }
    )
    assert config["framework"]["advanced"]["enable_fast_heap_query"] is False


def test_fast_heap_query_rejects_arduino(
    set_core_config: SetCoreConfigCallable,
) -> None:
    """Arduino's bundled SDK is outside the reviewed native source contract."""
    set_core_config(PlatformFramework.ESP32_ARDUINO)
    with pytest.raises(cv.Invalid, match="enable_fast_heap_query requires"):
        esp32.CONFIG_SCHEMA(
            {
                "variant": "ESP32S3",
                "framework": {
                    "type": "arduino",
                    "advanced": {"enable_fast_heap_query": True},
                },
            }
        )


@pytest.mark.parametrize("enabled", [None, False, True])
def test_fast_heap_query_codegen(
    tmp_path: Path,
    generate_main: Callable[[str | Path], str],
    enabled: bool | None,
) -> None:
    """Only explicit enablement registers the patch and its SDK configuration."""
    source = tmp_path / "fast-heap-query.yaml"
    advanced = (
        ""
        if enabled is None
        else f"    advanced:\n      enable_fast_heap_query: {str(enabled).lower()}\n"
    )
    source.write_text(
        "esphome:\n  name: fast-heap-query-test\n"
        "esp32:\n  variant: esp32p4\n  toolchain: esp-idf\n"
        f"  framework:\n    type: esp-idf\n    version: 5.5.6\n{advanced}",
        encoding="utf-8",
    )
    generate_main(source)
    components = CORE.data[KEY_ESP32][KEY_COMPONENTS]
    sdkconfig = CORE.data[KEY_ESP32][KEY_SDKCONFIG_OPTIONS]
    if enabled:
        assert components["fast_heap_query_framework_patch"] == {
            "repo": None,
            "ref": None,
            "path": (
                Path(esp32.__file__).parent / "fast_heap_query_framework_patch"
            ).as_posix(),
        }
        assert sdkconfig["CONFIG_ESPHOME_HEAP_FAST_LARGEST_BLOCK"] is True
    else:
        assert "fast_heap_query_framework_patch" not in components
        assert "CONFIG_ESPHOME_HEAP_FAST_LARGEST_BLOCK" not in sdkconfig
