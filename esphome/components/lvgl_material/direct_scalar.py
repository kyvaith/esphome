"""Opt-in bounded regional scalar; product provides all geometry and styling."""

from esphome import automation
import esphome.codegen as cg
from esphome.components.lvgl.defines import CONF_LVGL_ID
from esphome.components.lvgl.lvcode import LvContext, LvglComponent
from esphome.components.lvgl.types import (
    DirectSceneController,
    LvglApplication,
    LvglNavigation,
    lv_obj_t,
)
from esphome.components.lvgl.widgets import get_widgets
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import Lambda

from .entity_control import MaterialEntityControlComponent, strict_integer

CONF_DIRECT_SCALAR_CONTROLS = "direct_scalar_controls"
lvgl_material_ns = cg.esphome_ns.namespace("lvgl_material")
MaterialDirectScalarControl = lvgl_material_ns.class_(
    "MaterialDirectScalarControl", cg.Component, DirectSceneController
)
ScalarResumeAction = lvgl_material_ns.class_(
    "MaterialDirectScalarResumeAction", automation.Action
)
request_admission_slot = cg.slot_counter(
    "ESPHOME_LVGL_NAVIGATION_ADMISSION_GUARD_COUNT"
)
WIDGETS = ("region", "track", "bubble", "value_label", "minimum_label", "maximum_label")


def distinct(config):
    if len({str(config[name]) for name in WIDGETS}) != len(WIDGETS):
        raise cv.Invalid("Scalar widget bindings must be distinct")
    return config


DIRECT_SCALAR_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MaterialDirectScalarControl),
            cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
            cv.Required("entity_control_id"): cv.use_id(MaterialEntityControlComponent),
            cv.Required("navigation_id"): cv.use_id(LvglNavigation),
            cv.Required("application_id"): cv.use_id(LvglApplication),
            **{cv.Required(name): cv.use_id(lv_obj_t) for name in WIDGETS},
            cv.Required("active_color"): cv.templatable(cv.hex_uint32_t),
            cv.Optional("enabled", default=False): cv.boolean,
            cv.Optional("performance_metrics", default=False): cv.boolean,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    distinct,
)


async def direct_scalars_to_code(config, generic_controls=()):
    for conf in config:
        cg.add_define("USE_LVGL_MATERIAL_DIRECT_SCALAR_CONTROL")
        cg.add_define("USE_LVGL_DIRECT_REGION_EXPECTED_GENERATION")
        cg.add_define("USE_LVGL_NAVIGATION_ADMISSION_GUARD")
        lvgl = await cg.get_variable(conf[CONF_LVGL_ID])
        control = await cg.get_variable(conf["entity_control_id"])
        navigation = await cg.get_variable(conf["navigation_id"])
        application = await cg.get_variable(conf["application_id"])
        if str(conf["entity_control_id"]) not in generic_controls:
            request_admission_slot(str(navigation))
        var = cg.new_Pvariable(conf[CONF_ID], lvgl, control, navigation, application)
        await cg.register_component(var, conf)
        cg.add(var.set_initial_enabled(conf["enabled"]))
        if conf["performance_metrics"]:
            cg.add_define("USE_LVGL_MATERIAL_SCALAR_PERFORMANCE_METRICS")
            cg.add(var.set_performance_metrics_enabled(True))
        active_color = conf["active_color"]
        if isinstance(active_color, Lambda):
            provider = await cg.process_lambda(active_color, [], return_type=cg.uint32)
            cg.add(var.set_active_color_provider(provider))
        else:
            cg.add(var.set_active_color(active_color))
        widgets = [(await get_widgets(conf, name))[0] for name in WIDGETS]
        async with LvContext() as ctx:
            ctx.add(var.set_widgets(*(widget.obj for widget in widgets)))


RESUME_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(MaterialDirectScalarControl),
        cv.Required("generation"): cv.templatable(strict_integer(1, 2**32 - 1)),
    }
)


@automation.register_action(
    "lvgl_material.direct_scalar.resume",
    ScalarResumeAction,
    RESUME_SCHEMA,
    synchronous=True,
)
async def resume_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    cg.add(
        var.set_generation(await cg.templatable(config["generation"], args, cg.uint32))
    )
    return var
