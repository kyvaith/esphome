import os
from pathlib import Path

from esphome import pins
import esphome.codegen as cg
from esphome.components import esp32
from esphome.components.const import CONF_USE_PSRAM
import esphome.config_validation as cv
from esphome.const import (
    CONF_CLK_PIN,
    CONF_CS_PIN,
    CONF_FREQUENCY,
    CONF_MISO_PIN,
    CONF_MOSI_PIN,
    CONF_RESET_PIN,
    CONF_TYPE,
    CONF_VARIANT,
)
from esphome.core import CORE, CoroPriority, coroutine_with_priority
from esphome.cpp_generator import add_build_flag, add_define

CODEOWNERS = ["@swoboda1337"]
# esp32_ble raises the task watchdog around the remote BT controller bring-up
AUTO_LOAD = ["watchdog"]

CONF_ACTIVE_HIGH = "active_high"
CONF_BUS_WIDTH = "bus_width"
CONF_CMD_PIN = "cmd_pin"
CONF_D0_PIN = "d0_pin"
CONF_D1_PIN = "d1_pin"
CONF_D2_PIN = "d2_pin"
CONF_D3_PIN = "d3_pin"
CONF_DATA_READY_ACTIVE_HIGH = "data_ready_active_high"
CONF_DATA_READY_PIN = "data_ready_pin"
CONF_HANDSHAKE_ACTIVE_HIGH = "handshake_active_high"
CONF_HANDSHAKE_PIN = "handshake_pin"
CONF_PRESERVE_SDIO_TRANSACTION_ERRORS = "preserve_sdio_transaction_errors"
CONF_RESET_COPROCESSOR_ON_SHUTDOWN = "reset_coprocessor_on_shutdown"
CONF_SHUTDOWN_RESET_ID = "shutdown_reset_id"
CONF_RESTART_DIAGNOSTICS = "restart_diagnostics"
CONF_RESTART_DIAGNOSTICS_ID = "restart_diagnostics_id"
CONF_SDIO_FREQUENCY = "sdio_frequency"
CONF_SLOT = "slot"
CONF_SPI_MODE = "spi_mode"
CONF_TRANSPORT_DIAGNOSTICS = "transport_diagnostics"

esp32_hosted_ns = cg.esphome_ns.namespace("esp32_hosted")
RestartDiagnosticsComponent = esp32_hosted_ns.class_(
    "RestartDiagnosticsComponent", cg.Component
)

ShutdownResetComponent = esp32_hosted_ns.class_("ShutdownResetComponent", cg.Component)

# Shared fields for both transport modes
BASE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_VARIANT): cv.one_of(*esp32.VARIANTS, upper=True),
        cv.Required(CONF_ACTIVE_HIGH): cv.boolean,
        cv.Required(CONF_RESET_PIN): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_USE_PSRAM, default=False): cv.boolean,
        cv.Optional(CONF_RESTART_DIAGNOSTICS, default=False): cv.boolean,
        cv.Optional(CONF_TRANSPORT_DIAGNOSTICS, default=False): cv.boolean,
        cv.GenerateID(CONF_RESTART_DIAGNOSTICS_ID): cv.declare_id(
            RestartDiagnosticsComponent
        ),
    }
)

SDIO_SCHEMA = BASE_SCHEMA.extend(
    {
        cv.Optional(CONF_PRESERVE_SDIO_TRANSACTION_ERRORS, default=False): cv.boolean,
        cv.Optional(CONF_RESET_COPROCESSOR_ON_SHUTDOWN, default=False): cv.boolean,
        cv.GenerateID(CONF_SHUTDOWN_RESET_ID): cv.declare_id(ShutdownResetComponent),
        cv.Required(CONF_CLK_PIN): pins.internal_gpio_output_pin_number,
        cv.Required(CONF_CMD_PIN): pins.internal_gpio_output_pin_number,
        cv.Required(CONF_D0_PIN): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_D1_PIN): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_D2_PIN): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_D3_PIN): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_BUS_WIDTH, default=4): cv.one_of(1, 4, int=True),
        cv.Optional(CONF_SLOT, default=1): cv.int_range(min=0, max=1),
        cv.Optional(CONF_SDIO_FREQUENCY, default="40MHz"): cv.All(
            cv.frequency, cv.Range(min=400e3, max=50e6)
        ),
    }
)


def _validate_sdio(config):
    if config[CONF_RESET_COPROCESSOR_ON_SHUTDOWN] and esp32.idf_version() != cv.Version(
        5, 5, 5
    ):
        raise cv.Invalid(
            "reset_coprocessor_on_shutdown requires the reviewed ESP-IDF 5.5.5 lifecycle",
            path=[CONF_RESET_COPROCESSOR_ON_SHUTDOWN],
        )
    if config[
        CONF_PRESERVE_SDIO_TRANSACTION_ERRORS
    ] and esp32.idf_version() != cv.Version(5, 5, 5):
        raise cv.Invalid(
            "preserve_sdio_transaction_errors requires the reviewed "
            "ESP-IDF 5.5.5 source",
            path=[CONF_PRESERVE_SDIO_TRANSACTION_ERRORS],
        )
    if config[CONF_BUS_WIDTH] == 4:
        for pin in (CONF_D1_PIN, CONF_D2_PIN, CONF_D3_PIN):
            if pin not in config:
                raise cv.Invalid(
                    f"{pin} is required when bus_width is 4",
                    path=[pin],
                )
    return config


# SPI variant-dependent defaults and limits
_SPI_VARIANT_DEFAULTS = {
    "ESP32": {"spi_mode": 2, "frequency": 10, "max_frequency": 10},
    "ESP32C6": {"spi_mode": 3, "frequency": 26, "max_frequency": 40},
}
_SPI_DEFAULT = {"spi_mode": 3, "frequency": 40, "max_frequency": 40}

SPI_SCHEMA = BASE_SCHEMA.extend(
    {
        cv.Required(CONF_CLK_PIN): pins.internal_gpio_output_pin_number,
        cv.Required(CONF_MOSI_PIN): pins.internal_gpio_output_pin_number,
        cv.Required(CONF_MISO_PIN): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_CS_PIN): pins.internal_gpio_output_pin_number,
        cv.Required(CONF_HANDSHAKE_PIN): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_DATA_READY_PIN): pins.internal_gpio_input_pin_number,
        cv.Optional(CONF_SPI_MODE): cv.int_range(min=0, max=3),
        cv.Optional(CONF_FREQUENCY): cv.All(cv.frequency, cv.Range(min=1e6, max=40e6)),
        cv.Optional(CONF_HANDSHAKE_ACTIVE_HIGH, default=True): cv.boolean,
        cv.Optional(CONF_DATA_READY_ACTIVE_HIGH, default=True): cv.boolean,
    }
)


def _validate_spi(config):
    variant = config[CONF_VARIANT]
    defaults = _SPI_VARIANT_DEFAULTS.get(variant, _SPI_DEFAULT)

    if CONF_SPI_MODE not in config:
        config[CONF_SPI_MODE] = defaults["spi_mode"]

    if CONF_FREQUENCY not in config:
        config[CONF_FREQUENCY] = float(defaults["frequency"] * 1e6)

    freq_mhz = int(config[CONF_FREQUENCY] // 1e6)
    if freq_mhz > defaults["max_frequency"]:
        raise cv.Invalid(
            f"SPI frequency {freq_mhz}MHz exceeds maximum {defaults['max_frequency']}MHz for {variant}",
            path=[CONF_FREQUENCY],
        )
    return config


def _validate_transport_diagnostics(config):
    if config[CONF_TRANSPORT_DIAGNOSTICS] and not config[CONF_RESTART_DIAGNOSTICS]:
        raise cv.Invalid(
            "transport_diagnostics requires restart_diagnostics: true",
            path=[CONF_TRANSPORT_DIAGNOSTICS],
        )
    return config


def _remove_disabled_diagnostics_id(config):
    if not config[CONF_RESTART_DIAGNOSTICS]:
        config = config.copy()
        config.pop(CONF_RESTART_DIAGNOSTICS_ID)
    return config


def _remove_disabled_shutdown_reset_id(config):
    if config[CONF_TYPE] == "sdio" and not config[CONF_RESET_COPROCESSOR_ON_SHUTDOWN]:
        config = config.copy()
        config.pop(CONF_SHUTDOWN_RESET_ID)
    return config


CONFIG_SCHEMA = cv.All(
    cv.typed_schema(
        {
            "sdio": cv.All(SDIO_SCHEMA, _validate_sdio),
            "spi": cv.All(SPI_SCHEMA, _validate_spi),
        },
        default_type="sdio",
    ),
    _validate_transport_diagnostics,
    _remove_disabled_diagnostics_id,
    _remove_disabled_shutdown_reset_id,
)


def _configure_sdio(config, modern_hosted):
    if modern_hosted:
        esp32.add_idf_sdkconfig_option("CONFIG_ESP_HOSTED_SDIO_HOST_INTERFACE", True)
    slot = config[CONF_SLOT]
    esp32.add_idf_sdkconfig_option(
        f"CONFIG_ESP_HOSTED_SDIO_SLOT_{slot}",
        True,
    )
    if config[CONF_BUS_WIDTH] == 1:
        esp32.add_idf_sdkconfig_option("CONFIG_ESP_HOSTED_SDIO_1_BIT_BUS", True)
    else:
        esp32.add_idf_sdkconfig_option("CONFIG_ESP_HOSTED_SDIO_4_BIT_BUS", True)
    esp32.add_idf_sdkconfig_option(
        f"CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_CLK_SLOT_{slot}",
        config[CONF_CLK_PIN],
    )
    esp32.add_idf_sdkconfig_option(
        f"CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_CMD_SLOT_{slot}",
        config[CONF_CMD_PIN],
    )
    esp32.add_idf_sdkconfig_option(
        f"CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_D0_SLOT_{slot}",
        config[CONF_D0_PIN],
    )
    if config[CONF_BUS_WIDTH] == 4:
        esp32.add_idf_sdkconfig_option(
            f"CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_D1_4BIT_BUS_SLOT_{slot}",
            config[CONF_D1_PIN],
        )
        esp32.add_idf_sdkconfig_option(
            f"CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_D2_4BIT_BUS_SLOT_{slot}",
            config[CONF_D2_PIN],
        )
        esp32.add_idf_sdkconfig_option(
            f"CONFIG_ESP_HOSTED_PRIV_SDIO_PIN_D3_4BIT_BUS_SLOT_{slot}",
            config[CONF_D3_PIN],
        )
    esp32.add_idf_sdkconfig_option("CONFIG_ESP_HOSTED_CUSTOM_SDIO_PINS", True)
    esp32.add_idf_sdkconfig_option(
        "CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ",
        int(config[CONF_SDIO_FREQUENCY] // 1000),
    )


def _configure_spi(config):
    esp32.add_idf_sdkconfig_option("CONFIG_ESP_HOSTED_SPI_HOST_INTERFACE", True)
    # SPI mode is set via per-variant choice options
    variant = config[CONF_VARIANT]
    mode = config[CONF_SPI_MODE]
    suffix = "ESP32" if variant == "ESP32" else "ESP32XX"
    esp32.add_idf_sdkconfig_option(
        f"CONFIG_ESP_HOSTED_SPI_PRIV_MODE_{mode}_{suffix}",
        True,
    )
    # Frequency is set via per-variant options
    freq_mhz = int(config[CONF_FREQUENCY] // 1e6)
    if variant == "ESP32":
        esp32.add_idf_sdkconfig_option("CONFIG_ESP_HOSTED_SPI_FREQ_ESP32", freq_mhz)
    elif variant == "ESP32C6":
        esp32.add_idf_sdkconfig_option("CONFIG_ESP_HOSTED_SPI_FREQ_ESP32C6", freq_mhz)
    else:
        esp32.add_idf_sdkconfig_option("CONFIG_ESP_HOSTED_SPI_FREQ_ESP32XX", freq_mhz)
    # Pin configuration (use HSPI variant as P4/H2 hosts don't have VSPI)
    esp32.add_idf_sdkconfig_option(
        "CONFIG_ESP_HOSTED_SPI_HSPI_GPIO_MOSI", config[CONF_MOSI_PIN]
    )
    esp32.add_idf_sdkconfig_option(
        "CONFIG_ESP_HOSTED_SPI_HSPI_GPIO_MISO", config[CONF_MISO_PIN]
    )
    esp32.add_idf_sdkconfig_option(
        "CONFIG_ESP_HOSTED_SPI_HSPI_GPIO_CLK", config[CONF_CLK_PIN]
    )
    esp32.add_idf_sdkconfig_option(
        "CONFIG_ESP_HOSTED_SPI_HSPI_GPIO_CS", config[CONF_CS_PIN]
    )
    esp32.add_idf_sdkconfig_option(
        "CONFIG_ESP_HOSTED_SPI_GPIO_HANDSHAKE", config[CONF_HANDSHAKE_PIN]
    )
    esp32.add_idf_sdkconfig_option(
        "CONFIG_ESP_HOSTED_SPI_GPIO_DATA_READY", config[CONF_DATA_READY_PIN]
    )
    # Handshake and data_ready polarity
    if config[CONF_HANDSHAKE_ACTIVE_HIGH]:
        esp32.add_idf_sdkconfig_option("CONFIG_ESP_HOSTED_HS_ACTIVE_HIGH", True)
    else:
        esp32.add_idf_sdkconfig_option("CONFIG_ESP_HOSTED_HS_ACTIVE_LOW", True)
    if config[CONF_DATA_READY_ACTIVE_HIGH]:
        esp32.add_idf_sdkconfig_option("CONFIG_ESP_HOSTED_DR_ACTIVE_HIGH", True)
    else:
        esp32.add_idf_sdkconfig_option("CONFIG_ESP_HOSTED_DR_ACTIVE_LOW", True)


# Register diagnostics before safe_mode's generated early return. The transport
# configuration below emits build settings only, so its runtime order is unchanged.
@coroutine_with_priority(CoroPriority.FINAL)
async def _configure_transport_diagnostic_logging():
    # ESP32's normal code generation disables dynamic/tag-level logging. Apply
    # this explicit diagnostic opt-in afterwards without raising the default.
    esp32.add_idf_sdkconfig_option("CONFIG_LOG_MAXIMUM_EQUALS_DEFAULT", False)
    esp32.add_idf_sdkconfig_option("CONFIG_LOG_MAXIMUM_LEVEL_INFO", True)
    esp32.add_idf_sdkconfig_option("CONFIG_LOG_DYNAMIC_LEVEL_CONTROL", True)
    esp32.add_idf_sdkconfig_option("CONFIG_LOG_TAG_LEVEL_IMPL_NONE", False)
    esp32.add_idf_sdkconfig_option(
        "CONFIG_LOG_TAG_LEVEL_IMPL_CACHE_AND_LINKED_LIST", True
    )


@coroutine_with_priority(CoroPriority.DIAGNOSTICS)
async def to_code(config):
    add_define("USE_ESP32_HOSTED")
    if config.get(CONF_RESET_COPROCESSOR_ON_SHUTDOWN, False):
        add_define("USE_ESP32_HOSTED_SHUTDOWN_RESET")
        shutdown_reset = cg.new_Pvariable(config[CONF_SHUTDOWN_RESET_ID])
        await cg.register_component(shutdown_reset, config)
    if config[CONF_RESTART_DIAGNOSTICS]:
        add_define("USE_ESP32_HOSTED_RESTART_DIAGNOSTICS")
        add_build_flag("-Wl,--wrap=esp_restart")
        add_build_flag("-Wl,--wrap=hosted_restart_host")
        diagnostics = cg.new_Pvariable(config[CONF_RESTART_DIAGNOSTICS_ID])
        await cg.register_component(diagnostics, config)
    if config[CONF_TRANSPORT_DIAGNOSTICS]:
        add_define("USE_ESP32_HOSTED_TRANSPORT_DIAGNOSTICS")
        CORE.add_job(_configure_transport_diagnostic_logging)
    idf_ver = esp32.idf_version()
    modern_hosted = idf_ver >= cv.Version(5, 5, 0)
    transport = config[CONF_TYPE]
    transport_prefix = "SDIO" if transport == "sdio" else "SPI"

    # Reset polarity
    if config[CONF_ACTIVE_HIGH]:
        esp32.add_idf_sdkconfig_option(
            f"CONFIG_ESP_HOSTED_{transport_prefix}_RESET_ACTIVE_HIGH", True
        )
    else:
        esp32.add_idf_sdkconfig_option(
            f"CONFIG_ESP_HOSTED_{transport_prefix}_RESET_ACTIVE_LOW", True
        )
    # Reset GPIO
    esp32.add_idf_sdkconfig_option(
        f"CONFIG_ESP_HOSTED_{transport_prefix}_GPIO_RESET_SLAVE",  # NOLINT
        config[CONF_RESET_PIN],
    )
    # Slave variant  # NOLINT
    esp32.add_idf_sdkconfig_option(
        f"CONFIG_SLAVE_IDF_TARGET_{config[CONF_VARIANT]}",  # NOLINT
        True,
    )
    if modern_hosted:
        esp32.add_idf_sdkconfig_option(
            f"CONFIG_ESP_HOSTED_CP_TARGET_{config[CONF_VARIANT]}",  # NOLINT
            True,
        )

    # Transport-specific configuration
    if transport == "sdio":
        _configure_sdio(config, modern_hosted)
        if config[CONF_PRESERVE_SDIO_TRANSACTION_ERRORS]:
            esp32.add_idf_component(
                name="sdmmc_framework_patch",
                path=Path(__file__).with_name("sdmmc_framework_patch").as_posix(),
            )
            esp32.add_idf_sdkconfig_option(
                "CONFIG_ESPHOME_SDMMC_PRESERVE_TRANSACTION_ERROR", True
            )
    else:
        _configure_spi(config)

    # Place the transport mempool in PSRAM. Required on memory-tight host
    # configurations (e.g. P4 with a large LVGL UI) where the internal-RAM
    # mempool allocation fails at boot with `sdio_mempool_create` assert.
    if config[CONF_USE_PSRAM]:
        esp32.add_idf_sdkconfig_option("CONFIG_ESP_HOSTED_MEMPOOL_PREFER_SPIRAM", True)

    # Library versions
    os.environ["ESP_IDF_VERSION"] = f"{idf_ver.major}.{idf_ver.minor}"
    if idf_ver >= cv.Version(5, 5, 0):
        esp32.add_idf_component(name="espressif/esp_wifi_remote", ref="1.5.1")
        esp32.add_idf_component(name="espressif/wifi_remote_over_eppp", ref="0.3.2")
        esp32.add_idf_component(name="espressif/eppp_link", ref="1.1.5")
        esp32.add_idf_component(name="espressif/esp_hosted", ref="2.12.9")
    else:
        esp32.add_idf_component(name="espressif/esp_wifi_remote", ref="0.13.0")
        esp32.add_idf_component(name="espressif/eppp_link", ref="0.2.0")
        esp32.add_idf_component(name="espressif/esp_hosted", ref="2.0.11")
    esp32.add_extra_script(
        "post",
        "esp32_hosted.py",
        Path(__file__).parent / "esp32_hosted.py.script",
    )
