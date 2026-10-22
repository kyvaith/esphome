"""Keep reviewed transport options independent of upstream Hosted version support."""

import pytest

from esphome import config_validation as cv
from esphome.components.esp32.const import (
    KEY_BOARD,
    KEY_IDF_VERSION,
    KEY_VARIANT,
    VARIANT_ESP32P4,
)
from esphome.components.esp32_hosted import CONFIG_SCHEMA, _final_validate
from esphome.const import PlatformFramework

from ..types import SetCoreConfigCallable


def _sdio_config(**options: object) -> dict:
    return {
        "variant": "ESP32C6",
        "active_high": True,
        "reset_pin": 54,
        "clk_pin": 18,
        "cmd_pin": 19,
        "d0_pin": 14,
        "d1_pin": 15,
        "d2_pin": 16,
        "d3_pin": 17,
        **options,
    }


def _set_idf(
    set_core_config: SetCoreConfigCallable, version: str, hosted: str = ""
) -> None:
    set_core_config(
        PlatformFramework.ESP32_IDF,
        platform_data={
            KEY_BOARD: "esp32-p4-evboard",
            KEY_IDF_VERSION: cv.Version.parse(version),
            KEY_VARIANT: VARIANT_ESP32P4,
        },
        full_config={
            "esp32": {
                "framework": {
                    "components": [{"name": "espressif/esp_hosted", "ref": hosted}]
                }
            }
        },
    )


def test_reviewed_options_default_off_on_current_idf(
    set_core_config: SetCoreConfigCallable,
) -> None:
    """The optional backports do not narrow the ordinary upstream configuration."""
    _set_idf(set_core_config, "5.5.6")
    config = CONFIG_SCHEMA(_sdio_config())
    _final_validate(config)
    for name in (
        "preserve_sdio_transaction_errors",
        "reset_coprocessor_on_shutdown",
        "restart_diagnostics",
        "transport_diagnostics",
    ):
        assert config[name] is False
    assert "shutdown_reset_id" not in config
    assert "restart_diagnostics_id" not in config


@pytest.mark.parametrize(
    "option", ["preserve_sdio_transaction_errors", "reset_coprocessor_on_shutdown"]
)
def test_sdk_backports_reject_unreviewed_idf(
    set_core_config: SetCoreConfigCallable, option: str
) -> None:
    """A future SDK source cannot silently receive the old reviewed backport."""
    _set_idf(set_core_config, "5.5.7")
    with pytest.raises(cv.Invalid, match="reviewed.*5.5.5"):
        CONFIG_SCHEMA(_sdio_config(**{option: True}))


@pytest.mark.parametrize("idf", ["5.5.5", "5.5.6"])
def test_guarded_lifecycle_accepts_reviewed_transport(
    set_core_config: SetCoreConfigCallable,
    idf: str,
) -> None:
    """The product's reviewed SDK and Hosted major retain both backports."""
    _set_idf(set_core_config, idf, "2.12.13")
    config = CONFIG_SCHEMA(
        _sdio_config(
            preserve_sdio_transaction_errors=True,
            reset_coprocessor_on_shutdown=True,
            restart_diagnostics=True,
            transport_diagnostics=True,
        )
    )
    _final_validate(config)
    assert "shutdown_reset_id" in config
    assert "restart_diagnostics_id" in config


def test_transport_logging_requires_restart_diagnostics(
    set_core_config: SetCoreConfigCallable,
) -> None:
    """Transport logging cannot create an otherwise absent diagnostic owner."""
    _set_idf(set_core_config, "5.5.5")
    with pytest.raises(cv.Invalid, match="requires restart_diagnostics"):
        CONFIG_SCHEMA(_sdio_config(transport_diagnostics=True))


def test_upstream_hosted_3x_remains_available(
    set_core_config: SetCoreConfigCallable,
) -> None:
    """Hosted 3.x works when the reviewed 2.x shutdown extension is disabled."""
    _set_idf(set_core_config, "5.5.5", "3.0.0")
    _final_validate(CONFIG_SCHEMA(_sdio_config()))


def test_shutdown_reset_rejects_unreviewed_hosted_major(
    set_core_config: SetCoreConfigCallable,
) -> None:
    """The old private reset API is not assumed to work with Hosted 3.x."""
    _set_idf(set_core_config, "5.5.5", "3.0.0")
    config = CONFIG_SCHEMA(_sdio_config(reset_coprocessor_on_shutdown=True))
    with pytest.raises(cv.Invalid, match="reviewed esp_hosted 2.x"):
        _final_validate(config)
