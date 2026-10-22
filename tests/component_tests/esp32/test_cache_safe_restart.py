"""Reject cache-restart backports outside their reviewed SDK and chip scope."""

import pytest

from esphome import config_validation as cv
from esphome.components.esp32 import CONFIG_SCHEMA
from esphome.const import PlatformFramework, Toolchain
from tests.component_tests.types import SetCoreConfigCallable


@pytest.mark.parametrize(
    ("variant", "version", "toolchain", "enabled", "accepted"),
    [
        ("ESP32P4", "5.5.5", Toolchain.ESP_IDF, True, True),
        ("ESP32P4", "5.5.6", Toolchain.ESP_IDF, True, True),
        ("ESP32P4", "5.5.6", Toolchain.ESP_IDF, False, True),
        ("ESP32P4", "5.5.7", Toolchain.ESP_IDF, True, False),
        ("ESP32S3", "5.5.5", Toolchain.ESP_IDF, True, False),
        ("ESP32P4", "5.5.5", Toolchain.PLATFORMIO, True, False),
    ],
)
def test_cache_restart_scope(
    set_core_config: SetCoreConfigCallable,
    variant: str,
    version: str,
    toolchain: Toolchain,
    enabled: bool,
    accepted: bool,
) -> None:
    """The default remains portable, while enabling the backport is explicit and bounded."""
    set_core_config(PlatformFramework.ESP32_IDF)
    config = {
        "variant": variant,
        "toolchain": toolchain.value,
        "framework": {
            "type": "esp-idf",
            "version": version,
            "advanced": {"enable_cache_safe_restart": enabled},
        },
    }
    if accepted:
        assert (
            CONFIG_SCHEMA(config)["framework"]["advanced"]["enable_cache_safe_restart"]
            is enabled
        )
    else:
        with pytest.raises(
            cv.Invalid, match="requires ESP32-P4 with native ESP-IDF 5.5.5"
        ):
            CONFIG_SCHEMA(config)
