"""Sendspin generic_image platform."""

from esphome import automation
import esphome.codegen as cg
from esphome.components import runtime_image
from esphome.components.image import CONF_TRANSPARENCY, add_metadata
import esphome.config_validation as cv
from esphome.const import (
    CONF_BUFFER_SIZE,
    CONF_FORMAT,
    CONF_HEIGHT,
    CONF_ID,
    CONF_RESIZE,
    CONF_SOURCE,
    CONF_TRIGGER_ID,
    CONF_TYPE,
    CONF_WIDTH,
)
from esphome.core import CORE, ID
from esphome.cpp_generator import TemplateArgsType
from esphome.types import ConfigType

from .. import (
    CONF_REQUIRE_FRAME_DONE,
    CONF_SENDSPIN_ID,
    CONF_SLOT,
    IMAGE_FORMAT_BMP,
    IMAGE_FORMAT_JPEG,
    IMAGE_FORMAT_PNG,
    IMAGE_SOURCE_ALBUM,
    IMAGE_SOURCE_ARTIST,
    IMAGE_SOURCE_NONE,
    SendspinHub,
    register_artwork_preference,
    sendspin_ns,
)

AUTO_LOAD = ["runtime_image"]
CODEOWNERS = ["@kahrendt"]
DEPENDENCIES = ["sendspin"]

MAX_IMAGE_SLOTS = 4
_SLOT_COUNTER_KEY = "sendspin_image_slot_counter"

CONF_ON_IMAGE_DISPLAY = "on_image_display"
CONF_ON_IMAGE_ERROR = "on_image_error"
CONF_ON_DECODE_START = "on_decode_start"
CONF_DEFER_DECODE = "defer_decode"
CONF_PAUSED = "paused"
CONF_RETAIN_ON_CLEAR = "retain_on_clear"

# Map runtime_image's format string to the sendspin library's SendspinImageFormat enum.
_FORMAT_TO_SENDSPIN_ENUM = {
    "JPEG": IMAGE_FORMAT_JPEG,
    "PNG": IMAGE_FORMAT_PNG,
    "BMP": IMAGE_FORMAT_BMP,
}

IMAGE_SOURCES = {
    "ALBUM": IMAGE_SOURCE_ALBUM,
    "ARTIST": IMAGE_SOURCE_ARTIST,
    "NONE": IMAGE_SOURCE_NONE,
}

SendspinImage = sendspin_ns.class_(
    "SendspinImage",
    runtime_image.RuntimeImage,
    cg.Component,
)

SendspinImageDisplayTrigger = sendspin_ns.class_(
    "SendspinImageDisplayTrigger", automation.Trigger.template()
)
SendspinImageErrorTrigger = sendspin_ns.class_(
    "SendspinImageErrorTrigger", automation.Trigger.template()
)
SendspinImageDecodeStartTrigger = sendspin_ns.class_(
    "SendspinImageDecodeStartTrigger", automation.Trigger.template()
)
SendspinImagePauseAction = sendspin_ns.class_(
    "SendspinImagePauseAction", automation.Action
)
SendspinImageResumeAction = sendspin_ns.class_(
    "SendspinImageResumeAction", automation.Action
)


def _assign_slot_and_register(config: ConfigType) -> ConfigType:
    """Auto-assign a slot, validate the max count, and register the artwork preference with the hub."""
    current = CORE.data.get(_SLOT_COUNTER_KEY, 0)
    if current >= MAX_IMAGE_SLOTS:
        raise cv.Invalid(
            f"Too many Sendspin generic_image components. Maximum is {MAX_IMAGE_SLOTS}."
        )
    CORE.data[_SLOT_COUNTER_KEY] = current + 1
    config[CONF_SLOT] = current

    width, height = config[CONF_RESIZE]
    register_artwork_preference(
        {
            CONF_SLOT: current,
            CONF_SOURCE: config[CONF_SOURCE],
            CONF_FORMAT: _FORMAT_TO_SENDSPIN_ENUM[config[CONF_FORMAT]],
            CONF_WIDTH: width,
            CONF_HEIGHT: height,
            CONF_REQUIRE_FRAME_DONE: config[CONF_DEFER_DECODE],
        }
    )
    return config


def _validate_lifecycle(config: ConfigType) -> ConfigType:
    if config[CONF_PAUSED] and not config[CONF_DEFER_DECODE]:
        raise cv.Invalid("paused: true requires defer_decode: true")
    if config.get(CONF_ON_DECODE_START) and not config[CONF_DEFER_DECODE]:
        raise cv.Invalid("on_decode_start requires defer_decode: true")
    return config


CONFIG_SCHEMA = cv.All(
    runtime_image.runtime_image_schema(SendspinImage).extend(
        {
            cv.GenerateID(): cv.declare_id(SendspinImage),
            cv.GenerateID(CONF_SENDSPIN_ID): cv.use_id(SendspinHub),
            cv.Required(CONF_RESIZE): cv.dimensions,
            cv.Optional(CONF_SOURCE, default="ALBUM"): cv.enum(
                IMAGE_SOURCES, upper=True
            ),
            cv.Optional(CONF_DEFER_DECODE, default=False): cv.boolean,
            cv.Optional(CONF_PAUSED, default=False): cv.boolean,
            cv.Optional(CONF_RETAIN_ON_CLEAR, default=False): cv.boolean,
            cv.Optional(CONF_BUFFER_SIZE, default=0): cv.int_range(
                min=0, max=2 * 1024 * 1024
            ),
            cv.Optional(CONF_ON_DECODE_START): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                        SendspinImageDecodeStartTrigger
                    ),
                }
            ),
            cv.Optional(CONF_ON_IMAGE_DISPLAY): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                        SendspinImageDisplayTrigger
                    ),
                }
            ),
            cv.Optional(CONF_ON_IMAGE_ERROR): automation.validate_automation(
                {
                    cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(
                        SendspinImageErrorTrigger
                    ),
                }
            ),
        }
    ),
    runtime_image.validate_runtime_image_settings,
    _validate_lifecycle,
    cv.only_on_esp32,
    _assign_slot_and_register,
)

SENDSPIN_IMAGE_ACTION_SCHEMA = automation.maybe_simple_id(
    cv.Schema({cv.GenerateID(): cv.use_id(SendspinImage)})
)


@automation.register_action(
    "sendspin.image.pause",
    SendspinImagePauseAction,
    SENDSPIN_IMAGE_ACTION_SCHEMA,
    synchronous=True,
)
@automation.register_action(
    "sendspin.image.resume",
    SendspinImageResumeAction,
    SENDSPIN_IMAGE_ACTION_SCHEMA,
    synchronous=True,
)
async def sendspin_image_pause_resume_to_code(
    config: ConfigType,
    action_id: ID,
    template_arg: cg.TemplateArguments,
    args: TemplateArgsType,
):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


async def to_code(config: ConfigType) -> None:
    settings = await runtime_image.process_runtime_image_config(config)

    add_metadata(
        config[CONF_ID],
        settings.width,
        settings.height,
        config[CONF_TYPE],
        config[CONF_TRANSPARENCY],
    )

    var = cg.new_Pvariable(
        config[CONF_ID],
        settings.width,
        settings.height,
        settings.format_enum,
        settings.image_type_enum,
        settings.transparent,
        settings.byte_order_big_endian,
        settings.placeholder,
    )
    await cg.register_component(var, config)
    await cg.register_parented(var, config[CONF_SENDSPIN_ID])

    cg.add(var.set_decoder_type(settings.decoder_type_enum))
    cg.add(var.set_slot(config[CONF_SLOT]))
    cg.add(var.set_image_source(IMAGE_SOURCES[config[CONF_SOURCE]]))
    cg.add(var.set_deferred_decode(config[CONF_DEFER_DECODE]))
    cg.add(var.set_paused(config[CONF_PAUSED]))
    cg.add(var.set_retain_on_clear(config[CONF_RETAIN_ON_CLEAR]))
    cg.add(var.set_encoded_buffer_size(config[CONF_BUFFER_SIZE]))

    for conf in config.get(CONF_ON_DECODE_START, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)

    for conf in config.get(CONF_ON_IMAGE_DISPLAY, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)

    for conf in config.get(CONF_ON_IMAGE_ERROR, []):
        trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var)
        await automation.build_automation(trigger, [], conf)
