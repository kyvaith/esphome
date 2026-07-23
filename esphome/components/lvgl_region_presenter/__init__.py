from esphome import automation
import esphome.codegen as cg
from esphome.components.esp32 import VARIANT_ESP32P4, get_esp32_variant
from esphome.components.lvgl.defines import CONF_LVGL_ID
from esphome.components.lvgl.lvcode import LvglComponent
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.core import CORE

CODEOWNERS = ["@kyvaith"]
DEPENDENCIES = ["lvgl"]
MULTI_CONF = False

CONF_FRAME_INTERVAL = "frame_interval"

lvgl_region_presenter_ns = cg.esphome_ns.namespace("lvgl_region_presenter")
LvglRegionPresenter = lvgl_region_presenter_ns.class_(
    "LvglRegionPresenter", cg.Component
)
LvglRegionPresenterBeginAction = lvgl_region_presenter_ns.class_(
    "LvglRegionPresenterBeginAction",
    automation.Action,
    cg.Parented.template(LvglRegionPresenter),
)
LvglRegionPresenterEndAction = lvgl_region_presenter_ns.class_(
    "LvglRegionPresenterEndAction",
    automation.Action,
    cg.Parented.template(LvglRegionPresenter),
)


def _validate_platform(config):
    if not CORE.is_esp32 or get_esp32_variant() != VARIANT_ESP32P4:
        raise cv.Invalid("LVGL region presentation is only supported on ESP32-P4")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LvglRegionPresenter),
            cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
            cv.Optional(CONF_FRAME_INTERVAL, default="16ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(
                    min=cv.TimePeriod(milliseconds=8),
                    max=cv.TimePeriod(milliseconds=100),
                ),
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_platform,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    lvgl = await cg.get_variable(config[CONF_LVGL_ID])
    cg.add(var.set_lvgl_component(lvgl))
    cg.add(var.set_frame_interval(config[CONF_FRAME_INTERVAL].total_milliseconds))


PRESENTER_ACTION_SCHEMA = automation.maybe_simple_id(
    {
        cv.GenerateID(): cv.use_id(LvglRegionPresenter),
    }
)


@automation.register_action(
    "lvgl_region_presenter.begin",
    LvglRegionPresenterBeginAction,
    PRESENTER_ACTION_SCHEMA,
    synchronous=True,
)
@automation.register_action(
    "lvgl_region_presenter.end",
    LvglRegionPresenterEndAction,
    PRESENTER_ACTION_SCHEMA,
    synchronous=True,
)
async def presenter_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
