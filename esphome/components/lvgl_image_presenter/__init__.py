from esphome import automation
import esphome.codegen as cg
from esphome.components.esp32 import VARIANT_ESP32P4, get_esp32_variant
from esphome.components.image import Image_
from esphome.components.lvgl.defines import CONF_LVGL_ID
from esphome.components.lvgl.lvcode import LvContext, LvglComponent
from esphome.components.lvgl.types import lv_image_t
from esphome.components.lvgl.widgets import get_widgets, wait_for_widgets
import esphome.config_validation as cv
from esphome.const import CONF_DURATION, CONF_ID, CONF_SOURCE
from esphome.core import CORE

CODEOWNERS = ["@kyvaith"]
DEPENDENCIES = ["lvgl"]
MULTI_CONF = True

CONF_DIRECT = "direct"
CONF_CONTINUOUS = "continuous"
CONF_FADE_THROUGH_BLACK = "fade_through_black"
CONF_FRAME_INTERVAL = "frame_interval"
CONF_PAN_LIMIT = "pan_limit"
CONF_PHASE_DURATION = "phase_duration"
CONF_WIDGET = "widget"
CONF_ZOOM_END = "zoom_end"
CONF_ZOOM_START = "zoom_start"

lvgl_image_presenter_ns = cg.esphome_ns.namespace("lvgl_image_presenter")
LvglImagePresenter = lvgl_image_presenter_ns.class_("LvglImagePresenter", cg.Component)
LvglImagePresenterRestartAction = lvgl_image_presenter_ns.class_(
    "LvglImagePresenterRestartAction",
    automation.Action,
    cg.Parented.template(LvglImagePresenter),
)
LvglImagePresenterPauseAction = lvgl_image_presenter_ns.class_(
    "LvglImagePresenterPauseAction",
    automation.Action,
    cg.Parented.template(LvglImagePresenter),
)
LvglImagePresenterResumeAction = lvgl_image_presenter_ns.class_(
    "LvglImagePresenterResumeAction",
    automation.Action,
    cg.Parented.template(LvglImagePresenter),
)
LvglImagePresenterResetAction = lvgl_image_presenter_ns.class_(
    "LvglImagePresenterResetAction",
    automation.Action,
    cg.Parented.template(LvglImagePresenter),
)
LvglImagePresenterPauseForSnapshotAction = lvgl_image_presenter_ns.class_(
    "LvglImagePresenterPauseForSnapshotAction",
    automation.Action,
    cg.Parented.template(LvglImagePresenter),
)
LvglImagePresenterCompleteSnapshotHandoffAction = lvgl_image_presenter_ns.class_(
    "LvglImagePresenterCompleteSnapshotHandoffAction",
    automation.Action,
    cg.Parented.template(LvglImagePresenter),
)
LvglImagePresenterTransitionAction = lvgl_image_presenter_ns.class_(
    "LvglImagePresenterTransitionAction",
    automation.Action,
    cg.Parented.template(LvglImagePresenter),
)


def _validate_zoom(config):
    if config[CONF_ZOOM_END] < config[CONF_ZOOM_START]:
        raise cv.Invalid(f"{CONF_ZOOM_END} must not be lower than {CONF_ZOOM_START}")
    if config[CONF_DIRECT] and CONF_SOURCE not in config:
        raise cv.Invalid(f"{CONF_SOURCE} is required when {CONF_DIRECT} is enabled")
    if config[CONF_DIRECT] and config[CONF_ZOOM_END] != config[CONF_ZOOM_START]:
        raise cv.Invalid(
            "Direct image presentation currently supports panning at a fixed zoom only"
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LvglImagePresenter),
            cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
            cv.Required(CONF_WIDGET): cv.use_id(lv_image_t),
            cv.Optional(CONF_SOURCE): cv.use_id(Image_),
            cv.Optional(CONF_DIRECT, default=False): cv.boolean,
            cv.Optional(CONF_CONTINUOUS, default=False): cv.boolean,
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
            cv.Optional(CONF_ZOOM_START, default=1.0): cv.float_range(min=1.0, max=3.0),
            cv.Optional(CONF_ZOOM_END, default=1.125): cv.float_range(min=1.0, max=3.0),
            cv.Optional(CONF_PAN_LIMIT, default="75%"): cv.percentage,
            cv.Optional(CONF_FADE_THROUGH_BLACK, default=False): cv.boolean,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_zoom,
)


async def to_code(config):
    if config[CONF_DIRECT] and (
        not CORE.is_esp32 or get_esp32_variant() != VARIANT_ESP32P4
    ):
        raise cv.Invalid("Direct image presentation is only supported on ESP32-P4")

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    lvgl = await cg.get_variable(config[CONF_LVGL_ID])
    cg.add(var.set_lvgl_component(lvgl))
    if source_id := config.get(CONF_SOURCE):
        source = await cg.get_variable(source_id)
        cg.add(var.set_source(source))
    cg.add(var.set_phase_duration(config[CONF_PHASE_DURATION].total_milliseconds))
    cg.add(var.set_frame_interval(config[CONF_FRAME_INTERVAL].total_milliseconds))
    cg.add(var.set_direct(config[CONF_DIRECT]))
    cg.add(var.set_continuous(config[CONF_CONTINUOUS]))
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


PRESENTER_ACTION_SCHEMA = automation.maybe_simple_id(
    {
        cv.GenerateID(): cv.use_id(LvglImagePresenter),
    }
)


@automation.register_action(
    "lvgl_image_presenter.restart",
    LvglImagePresenterRestartAction,
    PRESENTER_ACTION_SCHEMA,
    synchronous=True,
)
@automation.register_action(
    "lvgl_image_presenter.pause",
    LvglImagePresenterPauseAction,
    PRESENTER_ACTION_SCHEMA,
    synchronous=True,
)
@automation.register_action(
    "lvgl_image_presenter.resume",
    LvglImagePresenterResumeAction,
    PRESENTER_ACTION_SCHEMA,
    synchronous=True,
)
@automation.register_action(
    "lvgl_image_presenter.reset",
    LvglImagePresenterResetAction,
    PRESENTER_ACTION_SCHEMA,
    synchronous=True,
)
@automation.register_action(
    "lvgl_image_presenter.pause_for_snapshot",
    LvglImagePresenterPauseForSnapshotAction,
    PRESENTER_ACTION_SCHEMA,
    synchronous=True,
)
@automation.register_action(
    "lvgl_image_presenter.complete_snapshot_handoff",
    LvglImagePresenterCompleteSnapshotHandoffAction,
    PRESENTER_ACTION_SCHEMA,
    synchronous=True,
)
async def presenter_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


PRESENTER_TRANSITION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(LvglImagePresenter),
        cv.Required(CONF_SOURCE): cv.use_id(Image_),
        cv.Optional(CONF_DURATION, default="800ms"): cv.templatable(
            cv.positive_time_period_milliseconds
        ),
    }
)


@automation.register_action(
    "lvgl_image_presenter.transition",
    LvglImagePresenterTransitionAction,
    PRESENTER_TRANSITION_SCHEMA,
    synchronous=True,
)
async def presenter_transition_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    source = await cg.get_variable(config[CONF_SOURCE])
    cg.add(var.set_source(source))
    duration = await cg.templatable(config[CONF_DURATION], args, cg.uint32)
    cg.add(var.set_duration(duration))
    return var
