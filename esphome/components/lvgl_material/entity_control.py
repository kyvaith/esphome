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
from esphome.components.time import RealTimeClock
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PAYLOAD, CONF_VALUE
from esphome.types import ConfigType

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
    "MaterialGenericEntityControlComponent",
    MaterialEntityControlComponent,
    DirectSceneController,
)
request_admission_slot = cg.slot_counter(
    "ESPHOME_LVGL_NAVIGATION_ADMISSION_GUARD_COUNT"
)
NUMBER_PROFILE_FIELDS = (
    "navigation_id",
    "application_id",
    "number_value_font",
    "minus_label",
    "plus_label",
    "state_label",
)
CONF_HUMIDIFIER_CONTROLLER = "humidifier_controller"
CONF_ROBOT_CONTROLLER = "robot_controller"
CONF_PANEL_CONTROLLER = "panel_controller"
CONF_DETAIL_CONTROLLER = "detail_controller"
CONF_CLIMATE_RANGE_PROFILE = "climate_range_profile"
CONF_LIGHT_COLOR_PROFILE = "light_color_profile"
LIGHT_COLOR_WIDGET_FIELDS = (
    "root",
    "kind",
    "title",
    "preview",
    "status",
    "hue_label",
    "hue_slider",
    "saturation_label",
    "saturation_slider",
    "cancel",
    "cancel_label",
    "apply",
    "apply_label",
    "main_button",
    "main_label",
)
CONF_EXTRA_CONTROLLER = "extra_controller"
CONF_AGENDA_PROFILE = "agenda_profile"
CONF_STATISTICS_PROFILE = "statistics_profile"
AGENDA_WIDGET_FIELDS = ("previous", "previous_label", "next", "next_label")
CONF_MEDIA_CONTROLLER = "media_controller"
CONF_ACTION_CONTROLLER = "action_controller"
CONF_TEXT_CONTROLLER = "text_controller"
TEXT_COMMON_WIDGET_FIELDS = (
    "root",
    "title",
    "kind",
    "icon",
    "surface",
    "viewport",
    "cursor",
    "count",
    "status",
    "cancel_label",
    "save_label",
)
TEXT_CONTROL_NAMES = (
    "up",
    "down",
    "shift",
    "backspace",
    "symbols",
    "alternate",
    "left",
    "space",
    "right",
    "cancel",
    "save",
)
TEXT_LAYOUT_FIELDS = (
    "surface_y",
    "surface_height",
    "viewport_y",
    "visible_lines",
    "count_y",
    "up_y",
    "down_y",
    "save_x",
)
TEXT_KEY_LABELS = ("symbols", "alternate", "space")
CONF_DATETIME_CONTROLLER = "datetime_controller"
DATETIME_COMMON_WIDGET_FIELDS = (
    "root",
    "title",
    "kind",
    "summary",
    "zone",
    "updated",
    "status",
    "fields_root",
    "editor_root",
    "editor_field",
    "editor_digits",
)
DATETIME_FIELD_NAMES = ("year", "month", "day", "hour", "minute", "second")
DATETIME_CONTROL_NAMES = ("minus", "plus", "cancel", "save")
ACTION_COMMON_WIDGET_FIELDS = (
    "root",
    "title",
    "kind",
    "phase",
    "last_used",
    "current",
    "policy",
    "status",
)
ACTION_ICON_FIELDS = ("button", "scene", "script", "automation")
MEDIA_COMMON_WIDGET_FIELDS = (
    "root",
    "title",
    "kind",
    "phase",
    "track",
    "artist",
    "status",
    "volume_summary",
)
MEDIA_VOLUME_WIDGET_FIELDS = ("root", "title", "kind", "observed", "status", "back")
MEDIA_CHOICE_WIDGET_FIELDS = (
    "root",
    "title",
    "kind",
    "current",
    "status",
    "back",
    "apply",
    "apply_label",
)
MEDIA_ICON_FIELDS = ("generic", "speaker", "tv", "receiver")
EXTRA_COMMON_WIDGET_FIELDS = (
    "root",
    "title_label",
    "kind_label",
    "updated_label",
    "status_label",
)
EXTRA_SENSOR_WIDGET_FIELDS = (
    "root",
    "value_label",
    "unit_label",
    "icon_unknown",
    "icon_temperature",
    "icon_humidity",
)
EXTRA_STATE_WIDGET_FIELDS = (
    "root",
    "state_label",
    "binary_surface",
    "alarm_surface",
    "lock_surface",
    "icon_unknown",
    "icon_moisture",
    "icon_alarm",
    "icon_locked",
    "icon_unlocked",
    "icon_calendar",
    "lock_button",
    "lock_label",
)
EXTRA_CALENDAR_WIDGET_FIELDS = (
    "root",
    "badge_label",
    "summary_label",
    "start_label",
    "end_label",
    "zone_label",
    "location_label",
    "description_label",
    "truncation_label",
)
EXTRA_PERSON_WIDGET_FIELDS = (
    "root",
    "icon",
    "surface",
    "state_label",
    "changed_label",
    "zone_label",
)
EXTRA_SUN_WIDGET_FIELDS = (
    "root",
    "icon",
    "phase_label",
    "rising_caption",
    "rising_value",
    "setting_caption",
    "setting_value",
    "elevation_caption",
    "elevation_value",
    "azimuth_caption",
    "azimuth_value",
    "zone_label",
)
EXTRA_PROFILES = (
    (
        "sensor",
        EXTRA_SENSOR_WIDGET_FIELDS,
        "compact_value_font",
        "set_extra_sensor_profile",
    ),
    (
        "state",
        EXTRA_STATE_WIDGET_FIELDS,
        "compact_state_font",
        "set_extra_state_profile",
    ),
    (
        "calendar",
        EXTRA_CALENDAR_WIDGET_FIELDS,
        "compact_summary_font",
        "set_extra_calendar_profile",
    ),
)
LIGHT_DETAIL_WIDGET_FIELDS = (
    "root",
    "kelvin_button",
    "kelvin_label",
    "effect_button",
    "effect_label",
    "status_label",
)
CLIMATE_DETAIL_WIDGET_FIELDS = (
    "root",
    "arc",
    "target_label",
    "current_label",
    "activity_label",
    "limits_label",
    "title_label",
    "knob",
    "minus_button",
    "plus_button",
    "state_label",
    "status_label",
    "mode_button",
    "mode_label",
    "parameters_button",
    "parameters_label",
    "parameters_root",
    "parameters_back_button",
)
DETAIL_NUMERIC_WIDGET_FIELDS = (
    "root",
    "title_label",
    "kind_label",
    "value_label",
    "current_label",
    "status_label",
    "track",
    "fill",
    "knob",
    "minimum_label",
    "maximum_label",
    "minus_button",
    "plus_button",
    "back_button",
    "kelvin_icon",
    "humidity_icon",
)
DETAIL_CHOICE_WIDGET_FIELDS = (
    "root",
    "title_label",
    "kind_label",
    "current_label",
    "status_label",
    "back_button",
    "apply_button",
    "apply_label",
)
DETAIL_PROFILES = (
    ("light", LIGHT_DETAIL_WIDGET_FIELDS, "set_light_detail_profile"),
    ("climate", CLIMATE_DETAIL_WIDGET_FIELDS, "set_climate_detail_profile"),
    ("numeric", DETAIL_NUMERIC_WIDGET_FIELDS, "set_detail_numeric_profile"),
    ("choice", DETAIL_CHOICE_WIDGET_FIELDS, "set_detail_choice_profile"),
)
TIMER_PANEL_WIDGET_FIELDS = (
    "root",
    "title_label",
    "kind_label",
    "state_label",
    "remaining_label",
    "status_label",
    "draft_hour_label",
    "draft_minute_label",
    "draft_second_label",
)
REMOTE_PANEL_WIDGET_FIELDS = (
    "root",
    "title_label",
    "kind_label",
    "state_label",
    "status_label",
    "power_button",
    "power_label",
    "activity_button",
    "activity_label",
    "activity_value_label",
    "options_root",
    "options_title_label",
    "back_button",
    "apply_button",
    "apply_label",
)
POWER_PANEL_WIDGET_FIELDS = (
    "root",
    "title_label",
    "kind_label",
    "state_label",
    "status_label",
    "power_button",
    "power_label",
    "icon_unknown",
    "icon_switch",
    "icon_outlet",
)

ROBOT_WIDGET_FIELDS = (
    "root",
    "title_label",
    "kind_label",
    "state_label",
    "status_label",
    "suction_button",
    "suction_label",
    "suction_root",
    "suction_back_button",
    "vacuum_icon",
    "mower_icon",
    "room_button",
    "room_label",
    "options_title_label",
    "room_apply_button",
    "room_apply_label",
    "battery_label",
)
HUMIDIFIER_WIDGET_FIELDS = (
    "root",
    "title_label",
    "kind_label",
    "target_label",
    "current_label",
    "state_label",
    "status_label",
    "track",
    "fill",
    "knob",
    "minimum_label",
    "maximum_label",
    "minus_button",
    "plus_button",
    "power_button",
    "power_label",
    "mode_button",
    "mode_label",
    "mode_root",
    "mode_back_button",
)
CONF_FAN_CONTROLLER = "fan_controller"
FAN_WIDGET_FIELDS = (
    "root",
    "title_label",
    "value_label",
    "state_label",
    "status_label",
    "power_button",
    "power_label",
    "oscillation_button",
    "oscillation_label",
    "direction_button",
    "direction_label",
    "preset_button",
    "preset_label",
    "preset_root",
    "preset_back_button",
)
CONF_COVER_CONTROLLER = "cover_controller"
CONF_THERMOSTAT_VIEW = "thermostat_view"
THERMOSTAT_WIDGET_FIELDS = (
    "root",
    "arc",
    "target",
    "current",
    "activity",
    "limits",
    "title",
    "knob",
    "minus",
    "plus",
)
COVER_WIDGET_FIELDS = ("root", "current_label", "status_label")
COVER_PANEL_WIDGET_FIELDS = (
    "title_label",
    "kind_label",
    "state_label",
    "position_group",
    "position_track",
    "position_fill",
    "position_knob",
    "tilt_group",
    "tilt_label",
    "tilt_track",
    "tilt_fill",
    "tilt_knob",
)
COVER_DEVICE_CLASSES = (
    "unknown",
    "awning",
    "blind",
    "curtain",
    "damper",
    "door",
    "garage",
    "gate",
    "shade",
    "shutter",
    "window",
)
CONF_CHOICE_SELECTOR = "choice_selector"
CONF_ALLOW_EMPTY_SELECTION = "allow_empty_selection"
CHOICE_WIDGET_FIELDS = (
    "root",
    "apply_button",
    "apply_label",
    "current_label",
    "status_label",
    "neutral_reference",
)
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


def _validate_control(config: ConfigType) -> ConfigType:
    if text := config.get(CONF_TEXT_CONTROLLER):
        if not config["number_stepper"]:
            raise cv.Invalid("text_controller requires number_stepper: true")
        names = [str(text[field]) for field in TEXT_COMMON_WIDGET_FIELDS]
        names += [
            str(widget) for field in ("lines", "keyboard") for widget in text[field]
        ]
        names += [
            str(widget)
            for field in ("controls", "key_labels")
            for widget in text[field].values()
        ]
        if len(names) != len(set(names)):
            raise cv.Invalid("Text widget bindings must be distinct within the profile")
        repertoire = text["repertoire"]
        if (
            not repertoire
            or len(repertoire) > 1024
            or len(set(repertoire)) != len(repertoire)
            or any(
                ord(c) < 32 or 0x7F <= ord(c) < 0xA0 or 0xD800 <= ord(c) <= 0xDFFF
                for c in repertoire
            )
        ):
            raise cv.Invalid(
                "Text repertoire must contain distinct printable Unicode scalars"
            )
        reachable = {" "}
        for keymap in text["maps"]:
            if len(keymap) != sum(text["row_lengths"]):
                raise cv.Invalid("Text map size must match configured row lengths")
            for key in keymap:
                if len(key["value"]) > 1 or any(
                    c not in repertoire for c in key["value"]
                ):
                    raise cv.Invalid(
                        "Text key value must be one admitted scalar or empty for alphabet return"
                    )
                if not key["label"] or any(ord(c) < 32 for c in key["label"]):
                    raise cv.Invalid("Text key labels must be printable and nonempty")
                reachable.update(key["value"])
        if not set(repertoire).issubset(reachable):
            raise cv.Invalid(
                "Every Text repertoire scalar must be reachable from the keyboard"
            )
        for layout in text["layouts"].values():
            if (
                layout["visible_lines"] < 1
                or layout["visible_lines"] > len(text["lines"])
                or layout["viewport_y"] + layout["visible_lines"] * text["line_height"]
                > layout["surface_height"]
            ):
                raise cv.Invalid("Text viewport must fit the configured surface")
    if datetime := config.get(CONF_DATETIME_CONTROLLER):
        if not config["number_stepper"]:
            raise cv.Invalid("datetime_controller requires number_stepper: true")
        names = [str(datetime[field]) for field in DATETIME_COMMON_WIDGET_FIELDS]
        names.extend(str(datetime["icons"][field]) for field in ("date", "time"))
        names.extend(
            str(datetime["fields"][name][field])
            for name in DATETIME_FIELD_NAMES
            for field in ("button", "caption", "value")
        )
        names.extend(str(widget) for widget in datetime["digits"])
        names.extend(
            str(datetime[field])
            for field in ("backspace", "accept", "cancel_label", "save_label")
        )
        names.extend(str(datetime["controls"][name]) for name in DATETIME_CONTROL_NAMES)
        if len(names) != len(set(names)):
            raise cv.Invalid(
                "Datetime widget bindings must be distinct within the profile"
            )
        for layout in datetime["field_layouts"].values():
            if layout["height"] < 2 * layout["field_height"]:
                raise cv.Invalid("Datetime field layout must fit both rows")
    if action := config.get(CONF_ACTION_CONTROLLER):
        if not config["number_stepper"]:
            raise cv.Invalid("action_controller requires number_stepper: true")
        if action["parameters"] and not action["enabled"]:
            raise cv.Invalid("parameters requires action_controller enabled")
        if action["tile_event"] and not action["enabled"]:
            raise cv.Invalid("tile_event requires action_controller enabled")
        names = [str(action[field]) for field in ACTION_COMMON_WIDGET_FIELDS]
        names.extend(str(action["icons"][field]) for field in ACTION_ICON_FIELDS)
        names.extend(
            str(option[field])
            for option in action["buttons"]
            for field in ("button", "label")
        )
        if len(set(names)) != len(names):
            raise cv.Invalid(
                "Action widget bindings must be distinct within the profile"
            )
    if media := config.get(CONF_MEDIA_CONTROLLER):
        if not config["number_stepper"]:
            raise cv.Invalid("media_controller requires number_stepper: true")
        names = [str(media[field]) for field in MEDIA_COMMON_WIDGET_FIELDS]
        names.extend(str(media["icons"][field]) for field in MEDIA_ICON_FIELDS)
        for family, fields in (
            ("volume", MEDIA_VOLUME_WIDGET_FIELDS),
            ("choice", MEDIA_CHOICE_WIDGET_FIELDS),
        ):
            names.extend(str(media[family][field]) for field in fields)
        for options in (media["actions"], media["facets"], media["choice"]["options"]):
            names.extend(
                str(option[field])
                for option in options
                for field in ("button", "label")
            )
        if len(set(names)) != len(names):
            raise cv.Invalid(
                "Media widget bindings must be distinct within the profile"
            )
    if extra := config.get(CONF_EXTRA_CONTROLLER):
        if not config["number_stepper"]:
            raise cv.Invalid("extra_controller requires number_stepper: true")
        if not extra["info"] and not extra["lock"]:
            raise cv.Invalid("extra_controller requires info or lock to be enabled")
        context_groups = "person" in extra and "sun" in extra
        if ("person" in extra or "sun" in extra) and not context_groups:
            raise cv.Invalid("Info context requires both person and sun widget groups")
        if extra["info_context"] and (not extra["info"] or not context_groups):
            raise cv.Invalid(
                "info_context requires info and both person and sun widget groups"
            )
        names = [str(extra[field]) for field in EXTRA_COMMON_WIDGET_FIELDS]
        for family, fields, _, _ in EXTRA_PROFILES:
            names.extend(str(extra[family][field]) for field in fields)
        if context_groups:
            for family, fields in (
                ("person", EXTRA_PERSON_WIDGET_FIELDS),
                ("sun", EXTRA_SUN_WIDGET_FIELDS),
            ):
                names.extend(str(extra[family][field]) for field in fields)
        if len(set(names)) != len(names):
            raise cv.Invalid("Extra widget bindings must be distinct across profiles")
    if agenda := config.get(CONF_AGENDA_PROFILE):
        extra = config.get(CONF_EXTRA_CONTROLLER)
        if not extra or not extra["info"]:
            raise cv.Invalid("agenda_profile requires extra_controller with info: true")
        agenda_names = [str(agenda[field]) for field in AGENDA_WIDGET_FIELDS]
        if len(set(agenda_names)) != len(agenda_names) or set(agenda_names) & set(
            names
        ):
            raise cv.Invalid(
                "Agenda widgets must be distinct from each other and Extra widgets"
            )
    if statistics := config.get(CONF_STATISTICS_PROFILE):
        extra = config.get(CONF_EXTRA_CONTROLLER)
        if not extra or not extra["info"]:
            raise cv.Invalid(
                "statistics_profile requires extra_controller with info: true"
            )
        bound_names = set(names)
        if agenda := config.get(CONF_AGENDA_PROFILE):
            bound_names.update(str(agenda[field]) for field in AGENDA_WIDGET_FIELDS)
        if str(statistics["surface"]) in bound_names:
            raise cv.Invalid(
                "Statistics surface must be distinct from Extra and Agenda widgets"
            )
    if detail := config.get(CONF_DETAIL_CONTROLLER):
        if not config["number_stepper"]:
            raise cv.Invalid("detail_controller requires number_stepper: true")
        if "light" not in detail and "climate" not in detail:
            raise cv.Invalid("detail_controller requires a light or climate profile")
        names = []
        for family, fields, _ in DETAIL_PROFILES:
            if family not in detail:
                continue
            profile = detail[family]
            names.extend(str(profile[field]) for field in fields)
            for group in ("parameter_buttons", "options"):
                names.extend(
                    str(option[field])
                    for option in profile.get(group, [])
                    for field in ("button", "label")
                )
        if len(set(names)) != len(names):
            raise cv.Invalid("Detail widget bindings must be distinct across profiles")
    if range_profile := config.get(CONF_CLIMATE_RANGE_PROFILE):
        if "climate" not in config.get(CONF_DETAIL_CONTROLLER, {}):
            raise cv.Invalid(
                "climate_range_profile requires the Climate detail_controller profile"
            )
        bindings = {str(range_profile[field]) for field in ("button", "label")}
        if len(bindings) != 2:
            raise cv.Invalid("Climate Range widget bindings must be distinct")
        for family, fields, _ in DETAIL_PROFILES:
            profile = config[CONF_DETAIL_CONTROLLER].get(family)
            if profile and bindings.intersection(
                str(profile[field]) for field in fields
            ):
                raise cv.Invalid(
                    "Climate Range widgets must not alias another Detail profile"
                )
    if color := config.get(CONF_LIGHT_COLOR_PROFILE):
        if "light" not in config.get(CONF_DETAIL_CONTROLLER, {}):
            raise cv.Invalid(
                "light_color_profile requires the Light detail_controller profile"
            )
        names = [str(color[field]) for field in LIGHT_COLOR_WIDGET_FIELDS]
        if len(set(names)) != len(names):
            raise cv.Invalid("Light Color widget bindings must be distinct")
        detail = config[CONF_DETAIL_CONTROLLER]
        for family, fields, _ in DETAIL_PROFILES:
            if family in detail and set(names).intersection(
                str(detail[family][field]) for field in fields
            ):
                raise cv.Invalid(
                    "Light Color widgets must not alias another Detail profile"
                )
    if thermostat := config.get(CONF_THERMOSTAT_VIEW):
        if not config["number_stepper"]:
            raise cv.Invalid("thermostat_view requires number_stepper: true")
        if len({str(thermostat[key]) for key in THERMOSTAT_WIDGET_FIELDS}) != len(
            THERMOSTAT_WIDGET_FIELDS
        ):
            raise cv.Invalid("Thermostat widget bindings must be distinct")
    if fan := config.get(CONF_FAN_CONTROLLER):
        if not config["number_stepper"]:
            raise cv.Invalid(
                "fan_controller requires the generic number_stepper profile"
            )
        names = [str(fan[name]) for name in FAN_WIDGET_FIELDS]
        if "speed_caption" in fan:
            names.append(str(fan["speed_caption"]))
        names.extend(
            str(option[name])
            for group in ("speed_buttons", "preset_options")
            for option in fan[group]
            for name in ("button", "label")
        )
        if len(set(names)) != len(names):
            raise cv.Invalid("Fan widget bindings must be distinct")
    if CONF_PANEL_CONTROLLER in config:
        panel = config[CONF_PANEL_CONTROLLER]
        if not config["number_stepper"] or not panel:
            raise cv.Invalid(
                "panel_controller requires number_stepper and at least one family"
            )
        names = []
        for family, fields in (
            ("timer", TIMER_PANEL_WIDGET_FIELDS),
            ("remote", REMOTE_PANEL_WIDGET_FIELDS),
            ("power", POWER_PANEL_WIDGET_FIELDS),
        ):
            if family not in panel:
                continue
            profile = panel[family]
            names.extend(str(profile[field]) for field in fields)
            if family == "timer":
                names.extend(str(widget) for widget in profile["draft_buttons"])
            for group in ("actions", "options"):
                names.extend(
                    str(option[field])
                    for option in profile.get(group, [])
                    for field in ("button", "label")
                )
        if len(set(names)) != len(names):
            raise cv.Invalid(
                "Panel widget bindings must be distinct across all families"
            )
    if robot := config.get(CONF_ROBOT_CONTROLLER):
        if not config["number_stepper"]:
            raise cv.Invalid(
                "robot_controller requires the generic number_stepper profile"
            )
        names = [str(robot[name]) for name in ROBOT_WIDGET_FIELDS]
        names.extend(
            str(option[name])
            for group in ("actions", "suction_options")
            for option in robot[group]
            for name in ("button", "label")
        )
        if len(set(names)) != len(names):
            raise cv.Invalid("Robot widget bindings must be distinct")
    if humidifier := config.get(CONF_HUMIDIFIER_CONTROLLER):
        if not config["number_stepper"]:
            raise cv.Invalid(
                "humidifier_controller requires the generic number_stepper profile"
            )
        names = [str(humidifier[name]) for name in HUMIDIFIER_WIDGET_FIELDS]
        names.extend(
            str(option[name])
            for group in ("mode_options",)
            for option in humidifier[group]
            for name in ("button", "label")
        )
        if len(set(names)) != len(names):
            raise cv.Invalid("Humidifier widget bindings must be distinct")
    if CONF_COVER_CONTROLLER in config:
        if not config["number_stepper"]:
            raise cv.Invalid(
                "cover_controller requires the generic number_stepper profile"
            )
        cover = config[CONF_COVER_CONTROLLER]
        cover_names = [str(cover[name]) for name in COVER_WIDGET_FIELDS]
        cover_names.extend(
            str(option[name])
            for option in cover["buttons"]
            for name in ("button", "label")
        )
        if panel := cover.get("panel"):
            if not cover["priority_stop"]:
                raise cv.Invalid("Cover panel requires priority_stop")
            cover_names.extend(str(panel[name]) for name in COVER_PANEL_WIDGET_FIELDS)
            cover_names.extend(
                str(option[name])
                for option in panel["tilt_buttons"]
                for name in ("button", "label")
            )
            classes = [icon["device_class"] for icon in panel["icons"]]
            if len(set(classes)) != len(classes):
                raise cv.Invalid("Cover panel icon classes must be distinct")
            if {str(icon["widget"]) for icon in panel["icons"]} & set(cover_names):
                raise cv.Invalid(
                    "Cover panel icons must be distinct from input widgets"
                )
        if len(set(cover_names)) != len(cover_names):
            raise cv.Invalid("Cover widget bindings must be distinct")
        if choice := config.get(CONF_CHOICE_SELECTOR):
            choice_names = [str(choice[name]) for name in CHOICE_WIDGET_FIELDS]
            choice_names.extend(
                str(option[name])
                for option in choice["options"]
                for name in ("button", "label")
            )
            if set(cover_names) & set(choice_names):
                raise cv.Invalid("Cover and Choice groups must be distinct")
    if CONF_CHOICE_SELECTOR in config:
        if not config["number_stepper"]:
            raise cv.Invalid(
                "choice_selector requires the generic number_stepper profile"
            )
        choice = config[CONF_CHOICE_SELECTOR]
        names = [str(choice[name]) for name in CHOICE_WIDGET_FIELDS]
        names.extend(
            str(option[name])
            for option in choice["options"]
            for name in ("button", "label")
        )
        if len(set(names)) != len(names):
            raise cv.Invalid("Choice widget bindings must be distinct")
    if config["number_stepper"]:
        if any(name not in config for name in NUMBER_PROFILE_FIELDS):
            raise cv.Invalid(
                "Number profile requires navigation, application, existing labels and full-glyph value font"
            )
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
            cv.Optional(CONF_TEXT_CONTROLLER): cv.Schema(
                {
                    cv.Optional("enabled", default=False): cv.boolean,
                    **{
                        cv.Required(field): cv.use_id(lv_obj_t)
                        for field in TEXT_COMMON_WIDGET_FIELDS
                    },
                    cv.Required("lines"): cv.All(
                        cv.ensure_list(cv.use_id(lv_obj_t)), cv.Length(min=6, max=6)
                    ),
                    cv.Required("keyboard"): cv.All(
                        cv.ensure_list(cv.use_id(lv_obj_t)), cv.Length(min=3, max=3)
                    ),
                    cv.Required("row_lengths"): cv.All(
                        cv.ensure_list(cv.int_range(min=1, max=16)),
                        cv.Length(min=3, max=3),
                    ),
                    cv.Required("controls"): cv.Schema(
                        {
                            cv.Required(name): cv.use_id(lv_obj_t)
                            for name in TEXT_CONTROL_NAMES
                        }
                    ),
                    cv.Required("key_labels"): cv.Schema(
                        {
                            cv.Required(name): cv.use_id(lv_obj_t)
                            for name in TEXT_KEY_LABELS
                        }
                    ),
                    cv.Required("repertoire"): cv.string_strict,
                    cv.Required("maps"): cv.All(
                        cv.ensure_list(
                            cv.ensure_list(
                                cv.Schema(
                                    {
                                        cv.Required("label"): cv.string_strict,
                                        cv.Required("value"): cv.string_strict,
                                    }
                                )
                            )
                        ),
                        cv.Length(min=6, max=6),
                    ),
                    cv.Required("layouts"): cv.Schema(
                        {
                            cv.Required(name): cv.Schema(
                                {
                                    cv.Required(field): cv.int_range(min=0, max=32767)
                                    for field in TEXT_LAYOUT_FIELDS
                                }
                            )
                            for name in ("overview", "editor")
                        }
                    ),
                    cv.Required("line_height"): cv.int_range(min=1, max=32767),
                }
            ),
            cv.Optional(CONF_DATETIME_CONTROLLER): cv.Schema(
                {
                    cv.Optional("enabled", default=False): cv.boolean,
                    **{
                        cv.Required(field): cv.use_id(lv_obj_t)
                        for field in DATETIME_COMMON_WIDGET_FIELDS
                    },
                    cv.Required("icons"): cv.Schema(
                        {
                            cv.Required(field): cv.use_id(lv_obj_t)
                            for field in ("date", "time")
                        }
                    ),
                    cv.Required("fields"): cv.Schema(
                        {
                            cv.Required(name): cv.Schema(
                                {
                                    cv.Required(field): cv.use_id(lv_obj_t)
                                    for field in ("button", "caption", "value")
                                }
                            )
                            for name in DATETIME_FIELD_NAMES
                        }
                    ),
                    cv.Required("digits"): cv.All(
                        cv.ensure_list(cv.use_id(lv_obj_t)), cv.Length(min=10, max=10)
                    ),
                    **{
                        cv.Required(field): cv.use_id(lv_obj_t)
                        for field in (
                            "backspace",
                            "accept",
                            "cancel_label",
                            "save_label",
                        )
                    },
                    cv.Required("controls"): cv.Schema(
                        {
                            cv.Required(name): cv.use_id(lv_obj_t)
                            for name in DATETIME_CONTROL_NAMES
                        }
                    ),
                    cv.Required("field_layouts"): cv.Schema(
                        {
                            cv.Required(name): cv.Schema(
                                {
                                    cv.Required("x"): cv.int_range(min=0, max=32767),
                                    cv.Required("y"): cv.int_range(min=0, max=32767),
                                    cv.Required("height"): cv.int_range(
                                        min=1, max=32767
                                    ),
                                    cv.Required("field_height"): cv.int_range(
                                        min=1, max=32767
                                    ),
                                }
                            )
                            for name in ("overview", "editor")
                        }
                    ),
                }
            ),
            cv.Optional(CONF_ACTION_CONTROLLER): cv.Schema(
                {
                    cv.Optional("enabled", default=False): cv.boolean,
                    cv.Optional("tile_event", default=False): cv.boolean,
                    cv.Optional("parameters", default=False): cv.boolean,
                    **{
                        cv.Required(field): cv.use_id(lv_obj_t)
                        for field in ACTION_COMMON_WIDGET_FIELDS
                    },
                    cv.Required("icons"): cv.Schema(
                        {
                            cv.Required(field): cv.use_id(lv_obj_t)
                            for field in ACTION_ICON_FIELDS
                        }
                    ),
                    cv.Required("buttons"): cv.All(
                        cv.ensure_list(
                            cv.Schema(
                                {
                                    cv.Required("button"): cv.use_id(lv_obj_t),
                                    cv.Required("label"): cv.use_id(lv_obj_t),
                                }
                            )
                        ),
                        cv.Length(min=3, max=3),
                    ),
                }
            ),
            cv.Optional(CONF_MEDIA_CONTROLLER): cv.Schema(
                {
                    cv.Optional("enabled", default=False): cv.boolean,
                    **{
                        cv.Required(field): cv.use_id(lv_obj_t)
                        for field in MEDIA_COMMON_WIDGET_FIELDS
                    },
                    **{
                        cv.Optional(field): static_number_font
                        for field in (
                            "track_compact_font",
                            "artist_compact_font",
                            "volume_compact_font",
                        )
                    },
                    cv.Required("icons"): cv.Schema(
                        {
                            cv.Required(field): cv.use_id(lv_obj_t)
                            for field in MEDIA_ICON_FIELDS
                        }
                    ),
                    **{
                        cv.Required(group): cv.All(
                            cv.ensure_list(
                                cv.Schema(
                                    {
                                        cv.Required("button"): cv.use_id(lv_obj_t),
                                        cv.Required("label"): cv.use_id(lv_obj_t),
                                    }
                                )
                            ),
                            cv.Length(min=count, max=count),
                        )
                        for group, count in (("actions", 12), ("facets", 3))
                    },
                    cv.Required("volume"): cv.Schema(
                        {
                            **{
                                cv.Required(field): cv.use_id(lv_obj_t)
                                for field in MEDIA_VOLUME_WIDGET_FIELDS
                            },
                            cv.Optional("compact_font"): static_number_font,
                        }
                    ),
                    cv.Required("choice"): cv.Schema(
                        {
                            **{
                                cv.Required(field): cv.use_id(lv_obj_t)
                                for field in MEDIA_CHOICE_WIDGET_FIELDS
                            },
                            cv.Optional("compact_font"): static_number_font,
                            cv.Required("options"): cv.All(
                                cv.ensure_list(
                                    cv.Schema(
                                        {
                                            cv.Required("button"): cv.use_id(lv_obj_t),
                                            cv.Required("label"): cv.use_id(lv_obj_t),
                                        }
                                    )
                                ),
                                cv.Length(min=8, max=8),
                            ),
                        }
                    ),
                }
            ),
            cv.Optional(CONF_EXTRA_CONTROLLER): cv.Schema(
                {
                    cv.Optional("info", default=False): cv.boolean,
                    cv.Optional("lock", default=False): cv.boolean,
                    cv.Optional("info_context", default=False): cv.boolean,
                    cv.Optional("person"): cv.Schema(
                        {
                            **{
                                cv.Required(field): cv.use_id(lv_obj_t)
                                for field in EXTRA_PERSON_WIDGET_FIELDS
                            },
                            cv.Optional("compact_state_font"): static_number_font,
                        }
                    ),
                    cv.Optional("sun"): cv.Schema(
                        {
                            cv.Required(field): cv.use_id(lv_obj_t)
                            for field in EXTRA_SUN_WIDGET_FIELDS
                        }
                    ),
                    **{
                        cv.Required(field): cv.use_id(lv_obj_t)
                        for field in EXTRA_COMMON_WIDGET_FIELDS
                    },
                    **{
                        cv.Required(family): cv.Schema(
                            {
                                **{
                                    cv.Required(field): cv.use_id(lv_obj_t)
                                    for field in fields
                                },
                                cv.Optional(font_field): static_number_font,
                            }
                        )
                        for family, fields, font_field, _ in EXTRA_PROFILES
                    },
                }
            ),
            cv.Optional(CONF_STATISTICS_PROFILE): cv.Schema(
                {
                    cv.Required("surface"): cv.use_id(lv_obj_t),
                    cv.Optional("enabled", default=False): cv.boolean,
                }
            ),
            cv.Optional(CONF_AGENDA_PROFILE): cv.Schema(
                {
                    cv.Optional("enabled", default=False): cv.boolean,
                    **{
                        cv.Required(field): cv.use_id(lv_obj_t)
                        for field in AGENDA_WIDGET_FIELDS
                    },
                }
            ),
            cv.Optional(CONF_CLIMATE_RANGE_PROFILE): cv.Schema(
                {
                    cv.Required("button"): cv.use_id(lv_obj_t),
                    cv.Required("label"): cv.use_id(lv_obj_t),
                    cv.Optional("enabled", default=False): cv.boolean,
                }
            ),
            cv.Optional(CONF_LIGHT_COLOR_PROFILE): cv.Schema(
                {
                    cv.Optional("enabled", default=False): cv.boolean,
                    cv.Optional("hit_padding", default=20): cv.int_range(min=0, max=64),
                    **{
                        cv.Required(field): cv.use_id(lv_obj_t)
                        for field in LIGHT_COLOR_WIDGET_FIELDS
                    },
                }
            ),
            cv.Optional(CONF_DETAIL_CONTROLLER): cv.Schema(
                {
                    cv.Optional("light"): cv.Schema(
                        {
                            cv.Required(field): cv.use_id(lv_obj_t)
                            for field in LIGHT_DETAIL_WIDGET_FIELDS
                        }
                    ),
                    cv.Optional("climate"): cv.Schema(
                        {
                            **{
                                cv.Required(field): cv.use_id(lv_obj_t)
                                for field in CLIMATE_DETAIL_WIDGET_FIELDS
                            },
                            cv.Required("parameter_buttons"): cv.All(
                                cv.ensure_list(
                                    cv.Schema(
                                        {
                                            cv.Required("button"): cv.use_id(lv_obj_t),
                                            cv.Required("label"): cv.use_id(lv_obj_t),
                                        }
                                    )
                                ),
                                cv.Length(min=5, max=5),
                            ),
                        }
                    ),
                    cv.Required("numeric"): cv.Schema(
                        {
                            cv.Required(field): cv.use_id(lv_obj_t)
                            for field in DETAIL_NUMERIC_WIDGET_FIELDS
                        }
                    ),
                    cv.Required("choice"): cv.Schema(
                        {
                            **{
                                cv.Required(field): cv.use_id(lv_obj_t)
                                for field in DETAIL_CHOICE_WIDGET_FIELDS
                            },
                            cv.Required("options"): cv.All(
                                cv.ensure_list(
                                    cv.Schema(
                                        {
                                            cv.Required("button"): cv.use_id(lv_obj_t),
                                            cv.Required("label"): cv.use_id(lv_obj_t),
                                        }
                                    )
                                ),
                                cv.Length(min=8, max=8),
                            ),
                        }
                    ),
                }
            ),
            cv.Optional(CONF_THERMOSTAT_VIEW): cv.Schema(
                {
                    cv.Required(name): cv.use_id(lv_obj_t)
                    for name in THERMOSTAT_WIDGET_FIELDS
                }
            ),
            cv.Optional(CONF_FAN_CONTROLLER): cv.Schema(
                {
                    cv.Optional("speed_caption"): cv.use_id(lv_obj_t),
                    **{
                        cv.Required(name): cv.use_id(lv_obj_t)
                        for name in FAN_WIDGET_FIELDS
                    },
                    **{
                        cv.Required(group): cv.All(
                            cv.ensure_list(
                                cv.Schema(
                                    {
                                        cv.Required("button"): cv.use_id(lv_obj_t),
                                        cv.Required("label"): cv.use_id(lv_obj_t),
                                    }
                                )
                            ),
                            cv.Length(min=8, max=8),
                        )
                        for group in ("speed_buttons", "preset_options")
                    },
                }
            ),
            cv.Optional(CONF_HUMIDIFIER_CONTROLLER): cv.Schema(
                {
                    **{
                        cv.Required(name): cv.use_id(lv_obj_t)
                        for name in HUMIDIFIER_WIDGET_FIELDS
                    },
                    **{
                        cv.Required(group): cv.All(
                            cv.ensure_list(
                                cv.Schema(
                                    {
                                        cv.Required("button"): cv.use_id(lv_obj_t),
                                        cv.Required("label"): cv.use_id(lv_obj_t),
                                    }
                                )
                            ),
                            cv.Length(min=8, max=8),
                        )
                        for group in ("mode_options",)
                    },
                }
            ),
            cv.Optional(CONF_PANEL_CONTROLLER): cv.Schema(
                {
                    cv.Optional("timer"): cv.Schema(
                        {
                            **{
                                cv.Required(field): cv.use_id(lv_obj_t)
                                for field in TIMER_PANEL_WIDGET_FIELDS
                            },
                            cv.Required("time_id"): cv.use_id(RealTimeClock),
                            cv.Required("draft_buttons"): cv.All(
                                cv.ensure_list(cv.use_id(lv_obj_t)),
                                cv.Length(min=6, max=6),
                            ),
                            cv.Required("actions"): cv.All(
                                cv.ensure_list(
                                    cv.Schema(
                                        {
                                            cv.Required("button"): cv.use_id(lv_obj_t),
                                            cv.Required("label"): cv.use_id(lv_obj_t),
                                        }
                                    )
                                ),
                                cv.Length(min=5, max=5),
                            ),
                        }
                    ),
                    cv.Optional("remote"): cv.Schema(
                        {
                            **{
                                cv.Required(field): cv.use_id(lv_obj_t)
                                for field in REMOTE_PANEL_WIDGET_FIELDS
                            },
                            cv.Required("options"): cv.All(
                                cv.ensure_list(
                                    cv.Schema(
                                        {
                                            cv.Required("button"): cv.use_id(lv_obj_t),
                                            cv.Required("label"): cv.use_id(lv_obj_t),
                                        }
                                    )
                                ),
                                cv.Length(min=8, max=8),
                            ),
                        }
                    ),
                    cv.Optional("power"): cv.Schema(
                        {
                            cv.Required(field): cv.use_id(lv_obj_t)
                            for field in POWER_PANEL_WIDGET_FIELDS
                        }
                    ),
                }
            ),
            cv.Optional(CONF_ROBOT_CONTROLLER): cv.Schema(
                {
                    **{
                        cv.Required(name): cv.use_id(lv_obj_t)
                        for name in ROBOT_WIDGET_FIELDS
                    },
                    **{
                        cv.Required(group): cv.All(
                            cv.ensure_list(
                                cv.Schema(
                                    {
                                        cv.Required("button"): cv.use_id(lv_obj_t),
                                        cv.Required("label"): cv.use_id(lv_obj_t),
                                    }
                                )
                            ),
                            cv.Length(min=count, max=count),
                        )
                        for group, count in (("actions", 6), ("suction_options", 8))
                    },
                }
            ),
            cv.Optional(CONF_COVER_CONTROLLER): cv.Schema(
                {
                    cv.Optional("priority_stop", default=False): cv.boolean,
                    cv.Optional("panel"): cv.Schema(
                        {
                            **{
                                cv.Required(name): cv.use_id(lv_obj_t)
                                for name in COVER_PANEL_WIDGET_FIELDS
                            },
                            cv.Required("tilt_buttons"): cv.All(
                                cv.ensure_list(
                                    cv.Schema(
                                        {
                                            cv.Required("button"): cv.use_id(lv_obj_t),
                                            cv.Required("label"): cv.use_id(lv_obj_t),
                                        }
                                    )
                                ),
                                cv.Length(min=3, max=3),
                            ),
                            cv.Optional("icons", default=[]): cv.All(
                                cv.ensure_list(
                                    cv.Schema(
                                        {
                                            cv.Required("device_class"): cv.one_of(
                                                *COVER_DEVICE_CLASSES, lower=True
                                            ),
                                            cv.Required("widget"): cv.use_id(lv_obj_t),
                                        }
                                    )
                                ),
                                cv.Length(max=11),
                            ),
                        }
                    ),
                    **{
                        cv.Required(name): cv.use_id(lv_obj_t)
                        for name in COVER_WIDGET_FIELDS
                    },
                    cv.Required("buttons"): cv.All(
                        cv.ensure_list(
                            cv.Schema(
                                {
                                    cv.Required("button"): cv.use_id(lv_obj_t),
                                    cv.Required("label"): cv.use_id(lv_obj_t),
                                }
                            )
                        ),
                        cv.Length(min=3, max=3),
                    ),
                }
            ),
            cv.Optional(CONF_CHOICE_SELECTOR): cv.Schema(
                {
                    cv.Optional(CONF_ALLOW_EMPTY_SELECTION, default=False): cv.boolean,
                    **{
                        cv.Required(name): cv.use_id(lv_obj_t)
                        for name in CHOICE_WIDGET_FIELDS
                    },
                    cv.Required("options"): cv.All(
                        cv.ensure_list(
                            cv.Schema(
                                {
                                    cv.Required("button"): cv.use_id(lv_obj_t),
                                    cv.Required("label"): cv.use_id(lv_obj_t),
                                }
                            )
                        ),
                        cv.Length(min=8, max=8),
                    ),
                }
            ),
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


async def entity_controls_to_code(config: list[ConfigType]) -> None:
    for conf in config:
        if conf["number_stepper"]:
            cg.add_define("USE_LVGL_MATERIAL_NUMBER_STEPPER")
            cg.add_define("USE_LVGL_NAVIGATION_ADMISSION_GUARD")
        if CONF_DETAIL_CONTROLLER in conf:
            cg.add_define("USE_LVGL_MATERIAL_DETAIL_CONTROLS")
            cg.add_define("USE_LVGL_MATERIAL_PANEL_CONTROLS")
            cg.add_define("USE_LVGL_MATERIAL_THERMOSTAT")
        if CONF_STATISTICS_PROFILE in conf:
            cg.add_define("USE_LVGL_MATERIAL_INFO_STATISTICS")
        if CONF_AGENDA_PROFILE in conf:
            cg.add_define("USE_LVGL_MATERIAL_INFO_AGENDA")
        if CONF_CLIMATE_RANGE_PROFILE in conf:
            cg.add_define("USE_LVGL_MATERIAL_CLIMATE_RANGE")
        if CONF_LIGHT_COLOR_PROFILE in conf:
            cg.add_define("USE_LVGL_MATERIAL_LIGHT_COLOR")
        if CONF_EXTRA_CONTROLLER in conf:
            cg.add_define("USE_LVGL_MATERIAL_EXTRA_CONTROLS")
            cg.add_define("USE_LVGL_MATERIAL_PANEL_CONTROLS")
            if "person" in conf[CONF_EXTRA_CONTROLLER]:
                cg.add_define("USE_LVGL_MATERIAL_INFO_CONTEXT")
        if CONF_ACTION_CONTROLLER in conf:
            cg.add_define("USE_LVGL_MATERIAL_ACTION_CONTROLS")
            if conf[CONF_ACTION_CONTROLLER]["parameters"]:
                cg.add_define("USE_LVGL_MATERIAL_ACTION_PARAMETERS")
            if conf[CONF_ACTION_CONTROLLER]["tile_event"]:
                cg.add_define("USE_LVGL_MATERIAL_TILE_EVENT_CONTROLS")
            cg.add_define("USE_LVGL_MATERIAL_EXTRA_CONTROLS")
            cg.add_define("USE_LVGL_MATERIAL_PANEL_CONTROLS")
        if CONF_TEXT_CONTROLLER in conf:
            cg.add_define("USE_LVGL_MATERIAL_TEXT_CONTROLS")
            cg.add_define("USE_LVGL_MATERIAL_EXTRA_CONTROLS")
            cg.add_define("USE_LVGL_MATERIAL_PANEL_CONTROLS")
        if CONF_DATETIME_CONTROLLER in conf:
            cg.add_define("USE_LVGL_MATERIAL_DATETIME_CONTROLS")
            cg.add_define("USE_LVGL_MATERIAL_EXTRA_CONTROLS")
            cg.add_define("USE_LVGL_MATERIAL_PANEL_CONTROLS")
        if CONF_MEDIA_CONTROLLER in conf:
            cg.add_define("USE_LVGL_MATERIAL_MEDIA_CONTROLS")
            cg.add_define("USE_LVGL_MATERIAL_EXTRA_CONTROLS")
            cg.add_define("USE_LVGL_MATERIAL_PANEL_CONTROLS")
        if CONF_HUMIDIFIER_CONTROLLER in conf:
            cg.add_define("USE_LVGL_MATERIAL_HUMIDIFIER_CONTROLS")
        if CONF_PANEL_CONTROLLER in conf:
            cg.add_define("USE_LVGL_MATERIAL_PANEL_CONTROLS")
            if "timer" in conf[CONF_PANEL_CONTROLLER]:
                cg.add_define("USE_LVGL_MATERIAL_TIMER_PANEL")
        if CONF_ROBOT_CONTROLLER in conf:
            cg.add_define("USE_LVGL_MATERIAL_ROBOT_CONTROLS")
        if CONF_FAN_CONTROLLER in conf:
            cg.add_define("USE_LVGL_MATERIAL_FAN_CONTROLS")
        if CONF_COVER_CONTROLLER in conf:
            cg.add_define("USE_LVGL_MATERIAL_COVER_CONTROLS")
            if conf[CONF_COVER_CONTROLLER]["priority_stop"]:
                cg.add_define("USE_LVGL_MATERIAL_COVER_PRIORITY_STOP")
            if "panel" in conf[CONF_COVER_CONTROLLER]:
                cg.add_define("USE_LVGL_MATERIAL_COVER_PANEL")
        if CONF_CHOICE_SELECTOR in conf:
            cg.add_define("USE_LVGL_MATERIAL_CHOICE_SELECTOR")
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
                ctx.add(
                    var.set_number_profile(
                        navigation, application, minus_label.obj, plus_label.obj, font
                    )
                )
            if fan := conf.get(CONF_FAN_CONTROLLER):
                bindings = [
                    (await get_widgets(fan, name))[0].obj for name in FAN_WIDGET_FIELDS
                ]
                ctx.add(var.set_fan_profile(*bindings))
                if "speed_caption" in fan:
                    ctx.add(
                        var.set_fan_speed_caption(
                            (await get_widgets(fan, "speed_caption"))[0].obj
                        )
                    )
                for group, method in (
                    ("speed_buttons", "set_fan_speed_button"),
                    ("preset_options", "set_fan_preset_option"),
                ):
                    for index, option in enumerate(fan[group]):
                        button = (await get_widgets(option, "button"))[0]
                        label = (await get_widgets(option, "label"))[0]
                        ctx.add(getattr(var, method)(index, button.obj, label.obj))
            if extra := conf.get(CONF_EXTRA_CONTROLLER):
                bindings = [
                    (await get_widgets(extra, field))[0].obj
                    for field in EXTRA_COMMON_WIDGET_FIELDS
                ]
                ctx.add(var.set_extra_profile(*bindings))
                for family, fields, font_field, method in EXTRA_PROFILES:
                    profile = extra[family]
                    bindings = [
                        (await get_widgets(profile, field))[0].obj for field in fields
                    ]
                    font = cg.nullptr
                    if font_field in profile:
                        font = await lvalid.lv_font.process(profile[font_field])
                        if not lvalid.is_lv_font(profile[font_field]):
                            font = cg.MockObj(font, "->").get_lv_font()
                    ctx.add(getattr(var, method)(*bindings, font))
                ctx.add(var.set_extra_profiles(extra["info"], extra["lock"]))
                if person := extra.get("person"):
                    bindings = [
                        (await get_widgets(person, field))[0].obj
                        for field in EXTRA_PERSON_WIDGET_FIELDS
                    ]
                    font = cg.nullptr
                    if "compact_state_font" in person:
                        font = await lvalid.lv_font.process(
                            person["compact_state_font"]
                        )
                        if not lvalid.is_lv_font(person["compact_state_font"]):
                            font = cg.MockObj(font, "->").get_lv_font()
                    ctx.add(var.set_extra_person_profile(*bindings, font))
                    bindings = [
                        (await get_widgets(extra["sun"], field))[0].obj
                        for field in EXTRA_SUN_WIDGET_FIELDS
                    ]
                    ctx.add(var.set_extra_sun_profile(*bindings))
                    ctx.add(var.set_extra_info_context(extra["info_context"]))
            if text := conf.get(CONF_TEXT_CONTROLLER):
                bindings = [
                    (await get_widgets(text, field))[0].obj
                    for field in TEXT_COMMON_WIDGET_FIELDS
                ]
                ctx.add(var.set_text_profile(*bindings))
                for index, widget in enumerate(text["lines"]):
                    ctx.add(
                        var.set_text_line(
                            index, (await get_widgets({CONF_ID: widget}))[0].obj
                        )
                    )
                for index, widget in enumerate(text["keyboard"]):
                    ctx.add(
                        var.set_text_keyboard(
                            index,
                            (await get_widgets({CONF_ID: widget}))[0].obj,
                            text["row_lengths"][index],
                        )
                    )
                for index, name in enumerate(TEXT_CONTROL_NAMES):
                    ctx.add(
                        var.set_text_control(
                            index, (await get_widgets(text["controls"], name))[0].obj
                        )
                    )
                for index, name in enumerate(TEXT_KEY_LABELS):
                    ctx.add(
                        var.set_text_key_label(
                            index, (await get_widgets(text["key_labels"], name))[0].obj
                        )
                    )
                prefix = str(conf[CONF_ID]) + "_text"
                for keymap_index, keymap in enumerate(text["maps"]):
                    offset = 0
                    for row, count in enumerate(text["row_lengths"]):
                        keys = keymap[offset : offset + count]
                        name = f"{prefix}_map_{keymap_index}_{row}"
                        labels = (
                            ", ".join(str(cg.safe_exp(k["label"])) for k in keys)
                            + ', ""'
                        )
                        values = ", ".join(
                            str(ord(k["value"]) if k["value"] else 0) for k in keys
                        )
                        ctx.add(
                            cg.RawStatement(
                                f"static const char *const {name}_labels[] = {{{labels}}};"
                            )
                        )
                        ctx.add(
                            cg.RawStatement(
                                f"static const uint32_t {name}_values[] = {{{values}}};"
                            )
                        )
                        ctx.add(
                            var.set_text_map(
                                keymap_index,
                                row,
                                cg.RawExpression(name + "_labels"),
                                cg.RawExpression(name + "_values"),
                            )
                        )
                        offset += count
                points = ", ".join(str(ord(c)) for c in sorted(text["repertoire"]))
                ctx.add(
                    cg.RawStatement(
                        f"static const uint32_t {prefix}_repertoire[] = {{{points}}};"
                    )
                )
                ctx.add(
                    var.set_text_repertoire(
                        cg.RawExpression(prefix + "_repertoire"),
                        len(text["repertoire"]),
                    )
                )
                for index, name in enumerate(("overview", "editor")):
                    ctx.add(
                        var.set_text_layout(
                            index,
                            *[
                                text["layouts"][name][field]
                                for field in TEXT_LAYOUT_FIELDS
                            ],
                        )
                    )
                ctx.add(var.set_text_line_height(text["line_height"]))
                if text["enabled"]:
                    navigation = await cg.get_variable(conf["navigation_id"])
                    application = await cg.get_variable(conf["application_id"])
                    ctx.add(
                        cg.RawStatement(
                            f"static const std::vector<esphome::lvgl::LvglApplication *> {prefix}_preview_apps{{{application}}};"
                        )
                    )
                    ctx.add(
                        cg.RawStatement(
                            f"{var}->set_text_preview_retire([]() {{ return {navigation}->try_prepare_application_snapshots({prefix}_preview_apps); }});"
                        )
                    )
                ctx.add(var.set_text_enabled(text["enabled"]))
            if datetime := conf.get(CONF_DATETIME_CONTROLLER):
                bindings = [
                    (await get_widgets(datetime, field))[0].obj
                    for field in DATETIME_COMMON_WIDGET_FIELDS
                ]
                ctx.add(var.set_datetime_profile(*bindings))
                for index, field in enumerate(("date", "time")):
                    ctx.add(
                        var.set_datetime_icon(
                            index, (await get_widgets(datetime["icons"], field))[0].obj
                        )
                    )
                for index, name in enumerate(DATETIME_FIELD_NAMES):
                    fields = [
                        (await get_widgets(datetime["fields"][name], field))[0].obj
                        for field in ("button", "caption", "value")
                    ]
                    ctx.add(var.set_datetime_field(index, *fields))
                for index, widget in enumerate(datetime["digits"]):
                    ctx.add(
                        var.set_datetime_key(
                            index, (await get_widgets({CONF_ID: widget}))[0].obj
                        )
                    )
                for index, field in enumerate(("backspace", "accept"), 10):
                    ctx.add(
                        var.set_datetime_key(
                            index, (await get_widgets(datetime, field))[0].obj
                        )
                    )
                for index, field in enumerate(DATETIME_CONTROL_NAMES):
                    ctx.add(
                        var.set_datetime_control(
                            index,
                            (await get_widgets(datetime["controls"], field))[0].obj,
                        )
                    )
                ctx.add(
                    var.set_datetime_labels(
                        (await get_widgets(datetime, "cancel_label"))[0].obj,
                        (await get_widgets(datetime, "save_label"))[0].obj,
                    )
                )
                for index, name in enumerate(("overview", "editor")):
                    layout = datetime["field_layouts"][name]
                    ctx.add(
                        var.set_datetime_field_layout(
                            index,
                            layout["x"],
                            layout["y"],
                            layout["height"],
                            layout["field_height"],
                        )
                    )
                ctx.add(var.set_datetime_enabled(datetime["enabled"]))
            if action := conf.get(CONF_ACTION_CONTROLLER):
                bindings = [
                    (await get_widgets(action, field))[0].obj
                    for field in ACTION_COMMON_WIDGET_FIELDS
                ]
                ctx.add(var.set_action_profile(*bindings))
                for index, field in enumerate(ACTION_ICON_FIELDS):
                    ctx.add(
                        var.set_action_icon(
                            index, (await get_widgets(action["icons"], field))[0].obj
                        )
                    )
                for index, option in enumerate(action["buttons"]):
                    button = (await get_widgets(option, "button"))[0].obj
                    label = (await get_widgets(option, "label"))[0].obj
                    ctx.add(var.set_action_button(index, button, label))
                ctx.add(var.set_action_enabled(action["enabled"]))
                if action["parameters"]:
                    ctx.add(var.set_action_parameters_enabled(True))
                if action["tile_event"]:
                    ctx.add(var.set_tile_event_enabled(True))
            if media := conf.get(CONF_MEDIA_CONTROLLER):
                bindings = [
                    (await get_widgets(media, field))[0].obj
                    for field in MEDIA_COMMON_WIDGET_FIELDS
                ]
                fonts = []
                for field in (
                    "track_compact_font",
                    "artist_compact_font",
                    "volume_compact_font",
                ):
                    font = cg.nullptr
                    if field in media:
                        font = await lvalid.lv_font.process(media[field])
                        if not lvalid.is_lv_font(media[field]):
                            font = cg.MockObj(font, "->").get_lv_font()
                    fonts.append(font)
                ctx.add(var.set_media_profile(*bindings, *fonts))
                for index, field in enumerate(MEDIA_ICON_FIELDS):
                    ctx.add(
                        var.set_media_icon(
                            index, (await get_widgets(media["icons"], field))[0].obj
                        )
                    )
                for group, method in (
                    ("actions", "set_media_action"),
                    ("facets", "set_media_facet"),
                ):
                    for index, option in enumerate(media[group]):
                        button = (await get_widgets(option, "button"))[0].obj
                        label = (await get_widgets(option, "label"))[0].obj
                        ctx.add(getattr(var, method)(index, button, label))
                for family, fields, method in (
                    ("volume", MEDIA_VOLUME_WIDGET_FIELDS, "set_media_volume_profile"),
                    ("choice", MEDIA_CHOICE_WIDGET_FIELDS, "set_media_choice_profile"),
                ):
                    profile = media[family]
                    bindings = [
                        (await get_widgets(profile, field))[0].obj for field in fields
                    ]
                    font = cg.nullptr
                    if "compact_font" in profile:
                        font = await lvalid.lv_font.process(profile["compact_font"])
                        if not lvalid.is_lv_font(profile["compact_font"]):
                            font = cg.MockObj(font, "->").get_lv_font()
                    ctx.add(getattr(var, method)(*bindings, font))
                for index, option in enumerate(media["choice"]["options"]):
                    button = (await get_widgets(option, "button"))[0].obj
                    label = (await get_widgets(option, "label"))[0].obj
                    ctx.add(var.set_media_choice_option(index, button, label))
                ctx.add(var.set_media_enabled(media["enabled"]))
            if detail := conf.get(CONF_DETAIL_CONTROLLER):
                for family, fields, method in DETAIL_PROFILES:
                    if family not in detail:
                        continue
                    profile = detail[family]
                    bindings = [
                        (await get_widgets(profile, field))[0].obj for field in fields
                    ]
                    ctx.add(getattr(var, method)(*bindings))
                    for group, setter in (
                        ("parameter_buttons", "set_detail_parameter"),
                        ("options", "set_detail_option"),
                    ):
                        for index, option in enumerate(profile.get(group, [])):
                            button = (await get_widgets(option, "button"))[0]
                            label = (await get_widgets(option, "label"))[0]
                            ctx.add(getattr(var, setter)(index, button.obj, label.obj))
            if statistics := conf.get(CONF_STATISTICS_PROFILE):
                surface = (await get_widgets(statistics, "surface"))[0]
                ctx.add(var.set_statistics_profile(surface.obj, statistics["enabled"]))
            if agenda := conf.get(CONF_AGENDA_PROFILE):
                bindings = [
                    (await get_widgets(agenda, field))[0].obj
                    for field in AGENDA_WIDGET_FIELDS
                ]
                ctx.add(var.set_agenda_profile(*bindings, agenda["enabled"]))
            if range_profile := conf.get(CONF_CLIMATE_RANGE_PROFILE):
                button = (await get_widgets(range_profile, "button"))[0]
                label = (await get_widgets(range_profile, "label"))[0]
                ctx.add(
                    var.set_climate_range_profile(
                        button.obj, label.obj, range_profile["enabled"]
                    )
                )
            if color := conf.get(CONF_LIGHT_COLOR_PROFILE):
                bindings = [
                    (await get_widgets(color, field))[0].obj
                    for field in LIGHT_COLOR_WIDGET_FIELDS
                ]
                ctx.add(
                    var.set_light_color_profile(
                        *bindings, color["hit_padding"], color["enabled"]
                    )
                )
            if panel := conf.get(CONF_PANEL_CONTROLLER):
                for family, fields in (
                    ("timer", TIMER_PANEL_WIDGET_FIELDS),
                    ("remote", REMOTE_PANEL_WIDGET_FIELDS),
                    ("power", POWER_PANEL_WIDGET_FIELDS),
                ):
                    if family not in panel:
                        continue
                    profile = panel[family]
                    bindings = [
                        (await get_widgets(profile, field))[0].obj for field in fields
                    ]
                    ctx.add(getattr(var, f"set_{family}_panel_profile")(*bindings))
                    if family == "timer":
                        clock = await cg.get_variable(profile["time_id"])
                        ctx.add(var.set_panel_time(clock))
                        for index, widget in enumerate(profile["draft_buttons"]):
                            bound = (await get_widgets({"button": widget}, "button"))[0]
                            ctx.add(var.set_timer_draft_button(index, bound.obj))
                    for group, method in (
                        ("actions", "set_timer_panel_action"),
                        ("options", "set_remote_panel_option"),
                    ):
                        for index, option in enumerate(profile.get(group, [])):
                            button = (await get_widgets(option, "button"))[0]
                            label = (await get_widgets(option, "label"))[0]
                            ctx.add(getattr(var, method)(index, button.obj, label.obj))
            if robot := conf.get(CONF_ROBOT_CONTROLLER):
                bindings = [
                    (await get_widgets(robot, name))[0].obj
                    for name in ROBOT_WIDGET_FIELDS
                ]
                ctx.add(var.set_robot_profile(*bindings))
                for group, method in (
                    ("actions", "set_robot_action"),
                    ("suction_options", "set_robot_option"),
                ):
                    for index, option in enumerate(robot[group]):
                        widget = (await get_widgets(option, "button"))[0]
                        label = (await get_widgets(option, "label"))[0]
                        ctx.add(getattr(var, method)(index, widget.obj, label.obj))
            if humidifier := conf.get(CONF_HUMIDIFIER_CONTROLLER):
                bindings = [
                    (await get_widgets(humidifier, name))[0].obj
                    for name in HUMIDIFIER_WIDGET_FIELDS
                ]
                ctx.add(var.set_humidifier_profile(*bindings))
                for group, method in (("mode_options", "set_humidifier_mode_option"),):
                    for index, option in enumerate(humidifier[group]):
                        button = (await get_widgets(option, "button"))[0]
                        label = (await get_widgets(option, "label"))[0]
                        ctx.add(getattr(var, method)(index, button.obj, label.obj))
            if cover := conf.get(CONF_COVER_CONTROLLER):
                if cover["priority_stop"]:
                    ctx.add(var.set_cover_priority_stop_enabled(True))
                bindings = [
                    (await get_widgets(cover, name))[0].obj
                    for name in COVER_WIDGET_FIELDS
                ]
                ctx.add(var.set_cover_profile(*bindings))
                for index, button in enumerate(cover["buttons"]):
                    widget = (await get_widgets(button, "button"))[0]
                    label = (await get_widgets(button, "label"))[0]
                    ctx.add(var.set_cover_button(index, widget.obj, label.obj))
                if panel := cover.get("panel"):
                    bindings = [
                        (await get_widgets(panel, name))[0].obj
                        for name in COVER_PANEL_WIDGET_FIELDS
                    ]
                    ctx.add(var.set_cover_panel_profile(*bindings))
                    for index, button in enumerate(panel["tilt_buttons"]):
                        widget = (await get_widgets(button, "button"))[0]
                        label = (await get_widgets(button, "label"))[0]
                        ctx.add(var.set_cover_tilt_button(index, widget.obj, label.obj))
                    for icon in panel["icons"]:
                        widget = (await get_widgets(icon, "widget"))[0]
                        ctx.add(
                            var.set_cover_panel_icon(
                                COVER_DEVICE_CLASSES.index(icon["device_class"]),
                                widget.obj,
                            )
                        )
            if thermostat := conf.get(CONF_THERMOSTAT_VIEW):
                cg.add_define("USE_LVGL_MATERIAL_THERMOSTAT")
                widgets = [
                    (await get_widgets(thermostat, name))[0].obj
                    for name in THERMOSTAT_WIDGET_FIELDS
                ]
                ctx.add(var.set_thermostat_view(*widgets))
            if choice := conf.get(CONF_CHOICE_SELECTOR):
                ctx.add(
                    var.set_choice_empty_enabled(choice[CONF_ALLOW_EMPTY_SELECTION])
                )
                bindings = [
                    (await get_widgets(choice, name))[0].obj
                    for name in CHOICE_WIDGET_FIELDS
                ]
                ctx.add(var.set_choice_profile(*bindings))
                for index, option in enumerate(choice["options"]):
                    button = (await get_widgets(option, "button"))[0]
                    label = (await get_widgets(option, "label"))[0]
                    ctx.add(var.set_choice_option(index, button.obj, label.obj))


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
        cv.Required("slot"): cv.templatable(strict_integer(2, 31)),
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
