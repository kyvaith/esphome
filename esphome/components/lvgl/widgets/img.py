import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ANGLE,
    CONF_ID,
    CONF_MODE,
    CONF_OFFSET_X,
    CONF_OFFSET_Y,
    CONF_ROTATION,
)

from ..defines import (
    CONF_ANTIALIAS,
    CONF_MAIN,
    CONF_PIVOT_X,
    CONF_PIVOT_Y,
    CONF_SCALE,
    CONF_SRC,
    CONF_ZOOM,
)
from ..lvcode import lv_add
from ..lv_validation import lv_angle, lv_bool, lv_image, scale, size
from ..types import KenBurnsController, lv_image_t
from . import Widget, WidgetType
from .label import CONF_LABEL

CONF_IMAGE = "image"
CONF_KEN_BURNS = "ken_burns"
CONF_DIRECT = "direct"
CONF_PHASE_DURATION = "phase_duration"
CONF_FRAME_INTERVAL = "frame_interval"
CONF_ZOOM_START = "zoom_start"
CONF_ZOOM_END = "zoom_end"
CONF_PAN_LIMIT = "pan_limit"


def validate_ken_burns(config):
    if config[CONF_ZOOM_END] < config[CONF_ZOOM_START]:
        raise cv.Invalid(f"{CONF_ZOOM_END} must not be lower than {CONF_ZOOM_START}")
    return config


KEN_BURNS_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(KenBurnsController),
            cv.Optional(CONF_DIRECT, default=False): cv.boolean,
            cv.Optional(CONF_PHASE_DURATION, default="18s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_FRAME_INTERVAL, default="33ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(min=cv.TimePeriod(milliseconds=16), max=cv.TimePeriod(milliseconds=100)),
            ),
            cv.Optional(CONF_ZOOM_START, default=1.0): cv.float_range(min=1.0, max=3.0),
            cv.Optional(CONF_ZOOM_END, default=1.125): cv.float_range(min=1.0, max=3.0),
            cv.Optional(CONF_PAN_LIMIT, default="75%"): cv.percentage,
        }
    ).extend(cv.COMPONENT_SCHEMA),
    validate_ken_burns,
)

BASE_IMG_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PIVOT_X): size,
        cv.Optional(CONF_PIVOT_Y): size,
        cv.Exclusive(CONF_ANGLE, CONF_ROTATION): lv_angle,
        cv.Exclusive(CONF_ROTATION, CONF_ROTATION): lv_angle,
        cv.Exclusive(CONF_ZOOM, CONF_SCALE): scale,
        cv.Exclusive(CONF_SCALE, CONF_SCALE): scale,
        cv.Optional(CONF_OFFSET_X): size,
        cv.Optional(CONF_OFFSET_Y): size,
        cv.Optional(CONF_ANTIALIAS): lv_bool,
        cv.Optional(CONF_MODE): cv.invalid(f"{CONF_MODE} is not supported in LVGL 9.x"),
        cv.Optional(CONF_KEN_BURNS): KEN_BURNS_SCHEMA,
    }
)

IMG_SCHEMA = BASE_IMG_SCHEMA.extend(
    {
        cv.Required(CONF_SRC): lv_image,  # ← AVANT
    }
)

IMG_MODIFY_SCHEMA = BASE_IMG_SCHEMA.extend(
    {
        cv.Optional(CONF_SRC): lv_image,
    }
)


class ImgType(WidgetType):
    def __init__(self):
        super().__init__(
            CONF_IMAGE,
            lv_image_t,
            (CONF_MAIN,),
            IMG_SCHEMA,
            IMG_MODIFY_SCHEMA,
        )

    def get_uses(self):
        return CONF_IMAGE, CONF_LABEL

    async def to_code(self, w: Widget, config):
        await w.set_property(CONF_SRC, await lv_image.process(config.get(CONF_SRC)))
        for prop, validator in BASE_IMG_SCHEMA.schema.items():
            if prop == CONF_KEN_BURNS:
                continue
            await w.set_property(prop, config, processor=validator)

        if ken_burns := config.get(CONF_KEN_BURNS):
            var = cg.new_Pvariable(ken_burns[CONF_ID])
            await cg.register_component(var, ken_burns)
            cg.add(var.set_phase_duration(ken_burns[CONF_PHASE_DURATION].total_milliseconds))
            cg.add(var.set_frame_interval(ken_burns[CONF_FRAME_INTERVAL].total_milliseconds))
            cg.add(var.set_direct(ken_burns[CONF_DIRECT]))
            cg.add(
                var.set_zoom(
                    round(ken_burns[CONF_ZOOM_START] * 256),
                    round(ken_burns[CONF_ZOOM_END] * 256),
                )
            )
            cg.add(var.set_pan_limit(ken_burns[CONF_PAN_LIMIT]))
            lv_add(var.set_obj(w.obj))


img_spec = ImgType()
