import esphome.codegen as cg
from esphome.components import esp32
from esphome.components.esp32 import VARIANT_ESP32P4, only_on_variant
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32"]
CODEOWNERS = ["@kyvaith"]

CONF_DECODER_TIMEOUT = "decoder_timeout"

esp32_jpeg_ns = cg.esphome_ns.namespace("esp32_jpeg")
Esp32JpegComponent = esp32_jpeg_ns.class_("Esp32JpegComponent", cg.Component)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(Esp32JpegComponent),
            cv.Optional(CONF_DECODER_TIMEOUT, default="180ms"): cv.positive_time_period_milliseconds,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
    cv.only_with_framework("esp-idf"),
    only_on_variant(supported=[VARIANT_ESP32P4]),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_decoder_timeout(config[CONF_DECODER_TIMEOUT].total_milliseconds))
    cg.add_define("USE_ESP32_JPEG")
    esp32.include_builtin_idf_component("esp_driver_jpeg")
