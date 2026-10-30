"""Opt-in bounded session and neutral LVGL input bindings."""

import re

from esphome import automation
import esphome.codegen as cg
from esphome.components.lvgl import lv_validation as lvalid
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
from esphome.const import CONF_ID, CONF_PAYLOAD, CONF_VALUE

lvgl_material_ns = cg.esphome_ns.namespace("lvgl_material")

CONF_ENTITY_CONTROLS = "entity_controls"
CONF_ON_MESSAGE = "on_message"
CONF_ON_GRANTED = "on_granted"
CONF_ON_CLOSED = "on_closed"
CONF_ON_VIEW_EVENT = "on_view_event"
CONF_ON_RENDER_REQUEST = "on_render_request"
CONF_ON_RENDER_CANCEL = "on_render_cancel"
CONF_REQUEST_ID = "request_id"
CONF_REASON = "reason"
CONF_TEXT_PROVIDER = "text_provider"
STATUS_KEYS = (
    "disabled",
    "waiting",
    "ready",
    "pending",
    "ha_accepted",
    "state_matches",
    "already_matched",
    "not_confirmed",
    "rejected",
    "expired",
    "renderer_unavailable",
    "connection_lost",
)
CLOSE_REASONS = (
    "user",
    "navigation",
    "sleep",
    "voice",
    "selection_changed",
    "connection_lost",
    "entity_unavailable",
    "capabilities_changed",
    "expired",
    "unload",
)
MaterialEntityControlComponent = lvgl_material_ns.class_(
    "MaterialEntityControlComponent", cg.Component
)
MaterialGenericEntityControlComponent = lvgl_material_ns.class_(
    "MaterialGenericEntityControlComponent", MaterialEntityControlComponent, DirectSceneController
)
request_admission_slot = cg.slot_counter("ESPHOME_LVGL_NAVIGATION_ADMISSION_GUARD_COUNT")
NUMBER_PROFILE_FIELDS = ("navigation_id", "application_id", "number_value_font", "minus_label", "plus_label", "state_label")
ControlReason = lvgl_material_ns.enum("ControlReason", is_class=True)
PayloadAction = lvgl_material_ns.class_(
    "MaterialEntityControlPayloadAction", automation.Action
)
OpenAction = lvgl_material_ns.class_(
    "MaterialEntityControlOpenAction", automation.Action
)
FlagAction = lvgl_material_ns.class_(
    "MaterialEntityControlFlagAction", automation.Action
)
InvalidateAction = lvgl_material_ns.class_(
    "MaterialEntityControlInvalidateAction", automation.Action
)
DisconnectAction = lvgl_material_ns.class_(
    "MaterialEntityControlDisconnectAction", automation.Action
)
RefreshTextsAction = lvgl_material_ns.class_(
    "MaterialEntityControlRefreshTextsAction", automation.Action
)
RenderReadyAction = lvgl_material_ns.class_(
    "MaterialEntityControlRenderReadyAction", automation.Action
)


def strict_integer(minimum, maximum):
    def validate(value):
        if (
            not isinstance(value, int)
            or isinstance(value, bool)
            or not minimum <= value <= maximum
        ):
            raise cv.Invalid(f"Expected integer between {minimum} and {maximum}")
        return value

    return validate


def bounded_text(value):
    value = cv.string_strict(value)
    try:
        encoded = value.encode("utf-8")
    except UnicodeError as err:
        raise cv.Invalid("Expected valid UTF-8 text") from err
    if len(encoded) > 160 or any(ord(c) < 32 or ord(c) == 127 for c in value):
        raise cv.Invalid("Text must contain at most 160 UTF-8 bytes without controls")
    return value


def card_id(value):
    value = cv.string_strict(value)
    if re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]{0,63}", value) is None:
        raise cv.Invalid("Expected bounded ASCII card ID")
    return value


def static_number_font(value):
    cv.check_not_templatable(value)
    return lvalid.lv_font(value)


def _validate_control(config):
    if config["number_stepper"]:
        if any(name not in config for name in NUMBER_PROFILE_FIELDS):
            raise cv.Invalid("Number profile requires navigation, application, existing labels and full-glyph value font")
        if str(config["minus_label"]) == str(config["plus_label"]):
            raise cv.Invalid("Number button labels must be distinct")
        config[CONF_ID].type = MaterialGenericEntityControlComponent
    elif any(name in config for name in NUMBER_PROFILE_FIELDS if name != "state_label"):
        raise cv.Invalid("Number-only bindings require number_stepper: true")
    names = (
        "root",
        "hit",
        "title_label",
        "value_label",
        "unknown_value_label",
        "on_button",
        "off_button",
    )
    if len({str(config[name]) for name in names}) != len(names):
        raise cv.Invalid("Control widget bindings must be distinct")
    if config["enabled"] and not config.get(CONF_ON_MESSAGE):
        raise cv.Invalid("Enabled control requires on_message transport automation")
    return config


ENTITY_CONTROL_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(MaterialEntityControlComponent),
            cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
            **{
                cv.Required(name): cv.use_id(lv_obj_t)
                for name in (
                    "root",
                    "hit",
                    "title_label",
                    "value_label",
                    "unknown_value_label",
                    "on_button",
                    "off_button",
                )
            },
            cv.Optional("state_label"): cv.use_id(lv_obj_t),
            cv.Optional("status_label"): cv.use_id(lv_obj_t),
            cv.Optional("enabled", default=False): cv.boolean,
            cv.Optional("number_stepper", default=False): cv.boolean,
            cv.Optional("navigation_id"): cv.use_id(LvglNavigation),
            cv.Optional("application_id"): cv.use_id(LvglApplication),
            cv.Optional("number_value_font"): static_number_font,
            cv.Optional("minus_label"): cv.use_id(lv_obj_t),
            cv.Optional("plus_label"): cv.use_id(lv_obj_t),
            cv.Optional("on_text", default="On"): bounded_text,
            cv.Optional("off_text", default="Off"): bounded_text,
            cv.Optional("status_texts", default={}): cv.Schema(
                {
                    cv.Optional(
                        key, default=key.replace("_", " ").capitalize()
                    ): bounded_text
                    for key in STATUS_KEYS
                }
            ),
            cv.Optional(CONF_TEXT_PROVIDER): cv.returning_lambda,
            **{
                cv.Optional(name): automation.validate_synchronous_automation(
                    f"lvgl_material entity control {name}",
                    "the borrowed payload and captured session data",
                )
                for name in (
                    CONF_ON_MESSAGE,
                    CONF_ON_GRANTED,
                    CONF_ON_CLOSED,
                    CONF_ON_VIEW_EVENT,
                    CONF_ON_RENDER_REQUEST,
                    CONF_ON_RENDER_CANCEL,
                )
            },
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_control,
)


async def entity_controls_to_code(config):
    for conf in config:
        if conf["number_stepper"]:
            cg.add_define("USE_LVGL_MATERIAL_NUMBER_STEPPER")
            cg.add_define("USE_LVGL_NAVIGATION_ADMISSION_GUARD")
        lvgl = await cg.get_variable(conf[CONF_LVGL_ID])
        var = cg.new_Pvariable(conf[CONF_ID], lvgl)
        await cg.register_component(var, conf)
        cg.add(var.set_initial_enabled(conf["enabled"]))
        cg.add(var.set_on_text(conf["on_text"]))
        cg.add(var.set_off_text(conf["off_text"]))
        for index, key in enumerate(STATUS_KEYS):
            cg.add(var.set_status_text(index, conf["status_texts"][key]))
        if provider := conf.get(CONF_TEXT_PROVIDER):
            value = await cg.process_lambda(
                provider, [(cg.uint8, "text_id")], return_type=cg.const_char_ptr
            )
            cg.add(var.set_text_provider(value))
        callbacks = (
            (
                CONF_ON_MESSAGE,
                "add_on_message_callback",
                [(cg.const_char_ptr, "payload")],
            ),
            (CONF_ON_GRANTED, "add_on_granted_callback", [(cg.uint32, "generation")]),
            (CONF_ON_CLOSED, "add_on_closed_callback", [(cg.uint32, "generation")]),
            (CONF_ON_VIEW_EVENT, "add_on_view_event_callback", [(cg.uint8, "event")]),
            (
                CONF_ON_RENDER_REQUEST,
                "add_on_render_request_callback",
                [(cg.uint32, "request_id"), (cg.bool_, "known"), (cg.uint8, "value")],
            ),
            (CONF_ON_RENDER_CANCEL, "add_on_render_cancel_callback", []),
        )
        for key, method, args in callbacks:
            for callback in conf.get(key, []):
                await automation.build_callback_automation(var, method, args, callback)
        widgets = [
            (await get_widgets(conf, name))[0]
            for name in (
                "root",
                "hit",
                "title_label",
                "value_label",
                "unknown_value_label",
                "on_button",
                "off_button",
            )
        ]
        async with LvContext() as ctx:
            ctx.add(var.set_widgets(*(widget.obj for widget in widgets)))
            for name in ("state_label", "status_label"):
                if name in conf:
                    widget = (await get_widgets(conf, name))[0]
                    ctx.add(getattr(var, "set_" + name)(widget.obj))
            if conf["number_stepper"]:
                navigation = await cg.get_variable(conf["navigation_id"])
                application = await cg.get_variable(conf["application_id"])
                request_admission_slot(str(navigation))
                minus_label = (await get_widgets(conf, "minus_label"))[0]
                plus_label = (await get_widgets(conf, "plus_label"))[0]
                font = await lvalid.lv_font.process(conf["number_value_font"])
                if not lvalid.is_lv_font(conf["number_value_font"]):
                    font = cg.MockObj(font, "->").get_lv_font()
                ctx.add(var.set_number_profile(navigation, application, minus_label.obj, plus_label.obj, font))


PAYLOAD_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(MaterialEntityControlComponent),
        cv.Required(CONF_PAYLOAD): cv.templatable(cv.string_strict),
    }
)


@automation.register_action(
    "lvgl_material.entity_control.bind", PayloadAction, PAYLOAD_SCHEMA, synchronous=True
)
async def bind_to_code(config, action_id, template_arg, args):
    return await _payload_to_code(config, action_id, template_arg, args, True)


@automation.register_action(
    "lvgl_material.entity_control.receive",
    PayloadAction,
    PAYLOAD_SCHEMA,
    synchronous=True,
)
async def receive_to_code(config, action_id, template_arg, args):
    return await _payload_to_code(config, action_id, template_arg, args, False)


async def _payload_to_code(config, action_id, template_arg, args, bind):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent, bind)
    cg.add(
        var.set_payload(await cg.templatable(config[CONF_PAYLOAD], args, cg.std_string))
    )
    return var


OPEN_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(MaterialEntityControlComponent),
        cv.Required("slot"): cv.templatable(strict_integer(2, 15)),
        cv.Required("card_id"): cv.templatable(card_id),
        cv.Required("revision"): cv.templatable(strict_integer(0, 2**53 - 1)),
    }
)


@automation.register_action(
    "lvgl_material.entity_control.open", OpenAction, OPEN_SCHEMA, synchronous=True
)
async def open_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    for name, type_ in (
        ("slot", cg.uint32),
        ("card_id", cg.std_string),
        ("revision", cg.uint64),
    ):
        cg.add(
            getattr(var, "set_" + name)(await cg.templatable(config[name], args, type_))
        )
    return var


FLAG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(MaterialEntityControlComponent),
        cv.Required(CONF_VALUE): cv.templatable(cv.boolean),
    }
)
VIEW_READY_SCHEMA = FLAG_SCHEMA.extend(
    {
        cv.Required("generation"): cv.templatable(strict_integer(1, 2**32 - 1)),
    }
)


@automation.register_action(
    "lvgl_material.entity_control.enable", FlagAction, FLAG_SCHEMA, synchronous=True
)
async def enable_to_code(config, action_id, template_arg, args):
    return await _flag_to_code(config, action_id, template_arg, args, True)


@automation.register_action(
    "lvgl_material.entity_control.view_ready",
    FlagAction,
    VIEW_READY_SCHEMA,
    synchronous=True,
)
async def view_ready_to_code(config, action_id, template_arg, args):
    return await _flag_to_code(config, action_id, template_arg, args, False)


async def _flag_to_code(config, action_id, template_arg, args, enable):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent, enable)
    cg.add(var.set_value(await cg.templatable(config[CONF_VALUE], args, cg.bool_)))
    if not enable:
        cg.add(
            var.set_generation(
                await cg.templatable(config["generation"], args, cg.uint32)
            )
        )
    return var


INVALIDATE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(MaterialEntityControlComponent),
        cv.Required(CONF_REASON): cv.one_of(*CLOSE_REASONS, lower=True),
    }
)


@automation.register_action(
    "lvgl_material.entity_control.invalidate",
    InvalidateAction,
    INVALIDATE_SCHEMA,
    synchronous=True,
)
async def invalidate_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    reason = getattr(ControlReason, "CONTROL_REASON_" + config[CONF_REASON].upper())
    return cg.new_Pvariable(action_id, template_arg, parent, reason)


PARENT_SCHEMA = cv.Schema({cv.GenerateID(): cv.use_id(MaterialEntityControlComponent)})


@automation.register_action(
    "lvgl_material.entity_control.disconnect",
    DisconnectAction,
    PARENT_SCHEMA,
    synchronous=True,
)
async def disconnect_to_code(config, action_id, template_arg, args):
    return cg.new_Pvariable(
        action_id, template_arg, await cg.get_variable(config[CONF_ID])
    )


@automation.register_action(
    "lvgl_material.entity_control.refresh_texts",
    RefreshTextsAction,
    PARENT_SCHEMA,
    synchronous=True,
)
async def refresh_texts_to_code(config, action_id, template_arg, args):
    return cg.new_Pvariable(
        action_id, template_arg, await cg.get_variable(config[CONF_ID])
    )


RENDER_READY_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(MaterialEntityControlComponent),
        cv.Required(CONF_REQUEST_ID): cv.templatable(strict_integer(1, 2**32 - 1)),
        cv.Required(CONF_VALUE): cv.templatable(cv.boolean),
    }
)


@automation.register_action(
    "lvgl_material.entity_control.render_ready",
    RenderReadyAction,
    RENDER_READY_SCHEMA,
    synchronous=True,
)
async def render_ready_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    cg.add(
        var.set_request_id(
            await cg.templatable(config[CONF_REQUEST_ID], args, cg.uint32)
        )
    )
    cg.add(var.set_value(await cg.templatable(config[CONF_VALUE], args, cg.bool_)))
    return var
