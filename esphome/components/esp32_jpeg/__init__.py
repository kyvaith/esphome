import esphome.codegen as cg
from esphome.components import esp32
from esphome.components.esp32 import VARIANT_ESP32P4, only_on_variant
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32"]
CODEOWNERS = ["@kyvaith"]

CONF_DECODER_TIMEOUT = "decoder_timeout"
CONF_DMA2D_AXI_BURSTINESS = "dma2d_axi_burstiness"
CONF_DMA2D_PEAK_LEVEL = "dma2d_peak_level"
CONF_DMA2D_TRANSACTION_LEVEL = "dma2d_transaction_level"
CONF_DMA2D_WRITE_PRIORITY = "dma2d_write_priority"
CONF_DMA2D_READ_PRIORITY = "dma2d_read_priority"
CONF_JPEG_DMA2D_AXI_BURSTINESS = "jpeg_dma2d_axi_burstiness"
CONF_JPEG_DMA2D_PEAK_LEVEL = "jpeg_dma2d_peak_level"
CONF_JPEG_DMA2D_TRANSACTION_LEVEL = "jpeg_dma2d_transaction_level"
CONF_JPEG_DMA2D_WRITE_PRIORITY = "jpeg_dma2d_write_priority"
CONF_JPEG_DMA2D_READ_PRIORITY = "jpeg_dma2d_read_priority"

esp32_jpeg_ns = cg.esphome_ns.namespace("esp32_jpeg")
Esp32JpegComponent = esp32_jpeg_ns.class_("Esp32JpegComponent", cg.Component)

def _validate_dma2d_qos(config):
    for peak_key, transaction_key in (
        (CONF_DMA2D_PEAK_LEVEL, CONF_DMA2D_TRANSACTION_LEVEL),
        (CONF_JPEG_DMA2D_PEAK_LEVEL, CONF_JPEG_DMA2D_TRANSACTION_LEVEL),
    ):
        if config[peak_key] >= config[transaction_key]:
            raise cv.Invalid(
                f"{peak_key} must be lower than {transaction_key}; "
                "ESP-IDF asserts when peak_level >= transaction_level"
            )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Esp32JpegComponent),
            cv.Optional(CONF_DECODER_TIMEOUT, default="180ms"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_DMA2D_AXI_BURSTINESS, default=8): cv.int_range(min=1, max=16),
            cv.Optional(CONF_DMA2D_PEAK_LEVEL, default=0): cv.int_range(min=0, max=10),
            cv.Optional(CONF_DMA2D_TRANSACTION_LEVEL, default=1): cv.int_range(min=1, max=11),
            cv.Optional(CONF_DMA2D_WRITE_PRIORITY, default=1): cv.int_range(min=0, max=3),
            cv.Optional(CONF_DMA2D_READ_PRIORITY, default=1): cv.int_range(min=0, max=3),
            cv.Optional(CONF_JPEG_DMA2D_AXI_BURSTINESS, default=1): cv.int_range(min=1, max=16),
            cv.Optional(CONF_JPEG_DMA2D_PEAK_LEVEL, default=2): cv.int_range(min=0, max=10),
            cv.Optional(CONF_JPEG_DMA2D_TRANSACTION_LEVEL, default=4): cv.int_range(min=1, max=11),
            cv.Optional(CONF_JPEG_DMA2D_WRITE_PRIORITY, default=0): cv.int_range(min=0, max=3),
            cv.Optional(CONF_JPEG_DMA2D_READ_PRIORITY, default=0): cv.int_range(min=0, max=3),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_dma2d_qos,
    cv.only_on_esp32,
    cv.only_with_framework("esp-idf"),
    only_on_variant(supported=[VARIANT_ESP32P4]),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_decoder_timeout(config[CONF_DECODER_TIMEOUT].total_milliseconds))
    cg.add_define("USE_ESP32_JPEG")
    cg.add_define("CONFIG_ESPHOME_DMA2D_AXI_BURSTINESS", config[CONF_DMA2D_AXI_BURSTINESS])
    cg.add_define("CONFIG_ESPHOME_DMA2D_PEAK_LEVEL", config[CONF_DMA2D_PEAK_LEVEL])
    cg.add_define("CONFIG_ESPHOME_DMA2D_TRANSACTION_LEVEL", config[CONF_DMA2D_TRANSACTION_LEVEL])
    cg.add_define("CONFIG_ESPHOME_DMA2D_WRITE_PRIORITY", config[CONF_DMA2D_WRITE_PRIORITY])
    cg.add_define("CONFIG_ESPHOME_DMA2D_READ_PRIORITY", config[CONF_DMA2D_READ_PRIORITY])
    cg.add_define("CONFIG_ESPHOME_JPEG_DMA2D_AXI_BURSTINESS", config[CONF_JPEG_DMA2D_AXI_BURSTINESS])
    cg.add_define("CONFIG_ESPHOME_JPEG_DMA2D_PEAK_LEVEL", config[CONF_JPEG_DMA2D_PEAK_LEVEL])
    cg.add_define(
        "CONFIG_ESPHOME_JPEG_DMA2D_TRANSACTION_LEVEL",
        config[CONF_JPEG_DMA2D_TRANSACTION_LEVEL],
    )
    cg.add_define("CONFIG_ESPHOME_JPEG_DMA2D_WRITE_PRIORITY", config[CONF_JPEG_DMA2D_WRITE_PRIORITY])
    cg.add_define("CONFIG_ESPHOME_JPEG_DMA2D_READ_PRIORITY", config[CONF_JPEG_DMA2D_READ_PRIORITY])
    esp32.include_builtin_idf_component("esp_driver_jpeg")
