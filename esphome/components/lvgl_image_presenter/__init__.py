import esphome.codegen as cg
from esphome.components.lvgl.lvcode import LvContext
from esphome.components.lvgl.types import lv_image_t
from esphome.components.lvgl.widgets import get_widgets, wait_for_widgets
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = ["@kyvaith"]
DEPENDENCIES = ["lvgl"]
MULTI_CONF = True

CONF_DIRECT = "direct"
CONF_FADE_THROUGH_BLACK = "fade_through_black"
CONF_FRAME_INTERVAL = "frame_interval"
CONF_PAN_LIMIT = "pan_limit"
CONF_PHASE_DURATION = "phase_duration"
CONF_WIDGET = "widget"
CONF_ZOOM_END = "zoom_end"
CONF_ZOOM_START = "zoom_start"

lvgl_image_presenter_ns = cg.esphome_ns.namespace("lvgl_image_presenter")
LvglImagePresenter = lvgl_image_presenter_ns.class_(
    "LvglImagePresenter", cg.Component
)


def _validate_zoom(config):
    if config[CONF_ZOOM_END] < config[CONF_ZOOM_START]:
        raise cv.Invalid(f"{CONF_ZOOM_END} must not be lower than {CONF_ZOOM_START}")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LvglImagePresenter),
            cv.Required(CONF_WIDGET): cv.use_id(lv_image_t),
            cv.Optional(CONF_DIRECT, default=False): cv.boolean,
            cv.Optional(
                CONF_PHASE_DURATION, default="18s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_FRAME_INTERVAL, default="33ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(
                    min=cv.TimePeriod(milliseconds=16),
                    max=cv.TimePeriod(milliseconds=100),
                ),
            ),
            cv.Optional(CONF_ZOOM_START, default=1.0): cv.float_range(
                min=1.0, max=3.0
            ),
            cv.Optional(CONF_ZOOM_END, default=1.125): cv.float_range(
                min=1.0, max=3.0
            ),
            cv.Optional(CONF_PAN_LIMIT, default="75%"): cv.percentage,
            cv.Optional(CONF_FADE_THROUGH_BLACK, default=False): cv.boolean,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_zoom,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_phase_duration(config[CONF_PHASE_DURATION].total_milliseconds))
    cg.add(var.set_frame_interval(config[CONF_FRAME_INTERVAL].total_milliseconds))
    cg.add(var.set_direct(config[CONF_DIRECT]))
    cg.add(
        var.set_zoom(
            round(config[CONF_ZOOM_START] * 256),
            round(config[CONF_ZOOM_END] * 256),
        )
    )
    cg.add(var.set_pan_limit(config[CONF_PAN_LIMIT]))
    cg.add(var.set_fade_through_black(config[CONF_FADE_THROUGH_BLACK]))

    widget = (await get_widgets(config, CONF_WIDGET))[0]
    await wait_for_widgets()
    async with LvContext() as ctx:
        ctx.add(var.set_obj(widget.obj))
