import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@kyvaith"]
AUTO_LOAD = ["json"]

immich_gallery_ns = cg.esphome_ns.namespace("immich_gallery")
ImmichGallery = immich_gallery_ns.class_("ImmichGallery", cg.Component)

CONF_IMAGE_SIZE = "image_size"
IMAGE_SIZES = {
    "preview": "preview",
    "fullsize": "fullsize",
    "original": "original",
}

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(ImmichGallery),
        cv.Optional(CONF_IMAGE_SIZE, default="preview"): cv.enum(
            IMAGE_SIZES, lower=True
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_image_size(config[CONF_IMAGE_SIZE]))
