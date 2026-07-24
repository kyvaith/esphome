import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@kyvaith"]
AUTO_LOAD = ["json"]

immich_gallery_ns = cg.esphome_ns.namespace("immich_gallery")
ImmichGallery = immich_gallery_ns.class_("ImmichGallery", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ImmichGallery),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
