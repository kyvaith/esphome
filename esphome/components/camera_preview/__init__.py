"""Timed requests to preview a keyed network camera from Home."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components.lvgl.types import LvglApplication
from esphome.components.network_camera import NetworkCamera
from esphome.const import CONF_ID

DEPENDENCIES = ["api", "lvgl", "network_camera"]
AUTO_LOAD = ["json"]

CameraPreview = cg.esphome_ns.namespace("camera_preview").class_(
    "CameraPreview", cg.Component
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(CameraPreview),
        cv.Required("camera"): cv.use_id(NetworkCamera),
        cv.Required("application"): cv.use_id(LvglApplication),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    camera = await cg.get_variable(config["camera"])
    application = await cg.get_variable(config["application"])
    var = cg.new_Pvariable(config[CONF_ID], camera, application)
    await cg.register_component(var, config)
