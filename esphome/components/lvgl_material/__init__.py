from esphome import automation
import esphome.codegen as cg
from esphome.components.lvgl.defines import CONF_LVGL_ID, CONF_WIDGETS
from esphome.components.lvgl.lv_validation import lv_color
from esphome.components.lvgl.lvcode import LvContext, LvglComponent
from esphome.components.lvgl.types import DirectSceneController, lv_obj_t
from esphome.components.lvgl.widgets import get_widgets, wait_for_widgets
import esphome.config_validation as cv
from esphome.const import (
    CONF_COLOR,
    CONF_COUNT,
    CONF_DURATION,
    CONF_ICON,
    CONF_ID,
    CONF_MESSAGE,
    CONF_PAYLOAD,
    CONF_VALUE,
)

CODEOWNERS = ["@kyvaith"]
DEPENDENCIES = ["lvgl"]
AUTO_LOAD = ["json"]

CONF_ACTIVE_COLOR = "active_color"
CONF_ACTIVE_SIZE = "active_size"
CONF_ACTIVATION_WIDGET = "activation_widget"
CONF_ANIMATIONS = "animations"
CONF_ANIMATION_LAYER = "animation_layer"
CONF_ALWAYS_HIDDEN = "always_hidden"
CONF_APPARENT_TEMPERATURE = "apparent_temperature"
CONF_ARC = "arc"
CONF_ARC_LENGTH = "arc_length"
CONF_BACKGROUND_COLOR = "background_color"
CONF_CLEAR = "clear"
CONF_CONTAINER = "container"
CONF_CONDITION = "condition"
CONF_CONDITION_LABEL = "condition_label"
CONF_DIRECT_MARQUEES = "direct_marquees"
CONF_DIRECT_SPINNERS = "direct_spinners"
CONF_DIRECT_STATE_LAYERS = "direct_state_layers"
CONF_DIRECT_VOLUME_OVERLAYS = "direct_volume_overlays"
CONF_DETAIL_LABEL = "detail_label"
CONF_ENTER_DURATION = "enter_duration"
CONF_EXIT_DURATION = "exit_duration"
CONF_GAP = "gap"
CONF_FRAME_RATE = "frame_rate"
CONF_GRADIENT_BOTTOM_COLOR = "gradient_bottom_color"
CONF_GRADIENT_START = "gradient_start"
CONF_INACTIVE_COLOR = "inactive_color"
CONF_INACTIVE_SIZE = "inactive_size"
CONF_INITIAL_PAGE = "initial_page"
CONF_INDICATOR_COLOR = "indicator_color"
CONF_IS_DAY = "is_day"
CONF_ICON_LABEL = "icon_label"
CONF_INTERACTION_WIDGET = "interaction_widget"
CONF_INTERACTION_STATE_LAYER = "interaction_state_layer"
CONF_KNOB = "knob"
CONF_KEY = "key"
CONF_PAGE_INDICATORS = "page_indicators"
CONF_NOTIFICATION_OVERLAYS = "notification_overlays"
CONF_ON_PRESS = "on_press"
CONF_ON_ANIMATION_COMPLETE = "on_animation_complete"
CONF_PRESSED_OPACITY = "pressed_opacity"
CONF_PRESSED_STYLES = "pressed_styles"
CONF_PRIMARY_COLOR = "primary_color"
CONF_SCRIM_OPACITY = "scrim_opacity"
CONF_SNAPSHOT_PAGE = "snapshot_page"
CONF_SNAPSHOT_REGION = "snapshot_region"
CONF_SNAPSHOT_REFRESH_DELAY = "snapshot_refresh_delay"
CONF_RESUME_DELAY = "resume_delay"
CONF_SECONDARY_COLOR = "secondary_color"
CONF_SCENE_CONTROLLERS = "scene_controllers"
CONF_STATE_LAYERS = "state_layers"
CONF_THICKNESS = "thickness"
CONF_TRANSITION_DURATION = "transition_duration"
CONF_TRACK_COLOR = "track_color"
CONF_TILE_SURFACES = "tile_surfaces"
CONF_TILES = "tiles"
CONF_TITLE = "title"
CONF_TITLE_LABEL = "title_label"
CONF_SUBTITLE_LABEL = "subtitle_label"
CONF_SLOT = "slot"
CONF_TERTIARY_COLOR = "tertiary_color"
CONF_TEMPERATURE = "temperature"
CONF_TEMPERATURE_LABEL = "temperature_label"
CONF_HUMIDITY = "humidity"
CONF_LABEL = "label"
CONF_ASSISTANT_LABEL = "assistant_label"
CONF_ROOT = "root"
CONF_STATUS_LABEL = "status_label"
CONF_SPIN_TIME = "spin_time"
CONF_USER_LABEL = "user_label"
CONF_VIEWPORT = "viewport"
CONF_VOICE_ASSISTANTS = "voice_assistants"
CONF_WEATHER_PRESENTERS = "weather_presenters"
CONF_WAVEFORM = "waveform"
CONF_WAVY_PROGRESS = "wavy_progress"
CONF_WIDGET = "widget"

lvgl_material_ns = cg.esphome_ns.namespace("lvgl_material")
MaterialStateLayer = lvgl_material_ns.class_("MaterialStateLayer", cg.Component)
MaterialDirectStateLayer = lvgl_material_ns.class_(
    "MaterialDirectStateLayer", cg.Component
)
MaterialDirectMarquee = lvgl_material_ns.class_("MaterialDirectMarquee", cg.Component)
MaterialDirectSpinner = lvgl_material_ns.class_("MaterialDirectSpinner", cg.Component)
MaterialDirectVolumeOverlay = lvgl_material_ns.class_(
    "MaterialDirectVolumeOverlay", cg.Component
)
MaterialWavyProgress = lvgl_material_ns.class_("MaterialWavyProgress", cg.Component)
MaterialVoiceAssistant = lvgl_material_ns.class_(
    "MaterialVoiceAssistant", cg.Component
)
MaterialPressedStyle = lvgl_material_ns.class_("MaterialPressedStyle", cg.Component)
MaterialPageIndicator = lvgl_material_ns.class_("MaterialPageIndicator", cg.Component)
MaterialTileSurface = lvgl_material_ns.class_("MaterialTileSurface", cg.Component)
MaterialNotificationOverlay = lvgl_material_ns.class_(
    "MaterialNotificationOverlay", cg.Component
)
MaterialWeatherPresenter = lvgl_material_ns.class_(
    "MaterialWeatherPresenter", cg.Component, DirectSceneController
)
MaterialPageIndicatorSetAction = lvgl_material_ns.class_(
    "MaterialPageIndicatorSetAction",
    automation.Action,
    cg.Parented.template(MaterialPageIndicator),
)
MaterialTileSurfaceConfigureAction = lvgl_material_ns.class_(
    "MaterialTileSurfaceConfigureAction", automation.Action
)
MaterialNotificationShowAction = lvgl_material_ns.class_(
    "MaterialNotificationShowAction", automation.Action
)
MaterialNotificationDismissAction = lvgl_material_ns.class_(
    "MaterialNotificationDismissAction", automation.Action
)
MaterialWeatherUpdateAction = lvgl_material_ns.class_(
    "MaterialWeatherUpdateAction", automation.Action
)
MaterialWeatherSuspendAction = lvgl_material_ns.class_(
    "MaterialWeatherSuspendAction", automation.Action
)
MaterialWeatherResumeAction = lvgl_material_ns.class_(
    "MaterialWeatherResumeAction", automation.Action
)
MaterialDirectSpinnerStartAction = lvgl_material_ns.class_(
    "MaterialDirectSpinnerStartAction", automation.Action
)
MaterialDirectSpinnerStopAction = lvgl_material_ns.class_(
    "MaterialDirectSpinnerStopAction", automation.Action
)

STATE_LAYER_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MaterialStateLayer),
        cv.Required(CONF_WIDGET): cv.use_id(lv_obj_t),
        cv.Optional(CONF_COLOR, default=0x000000): lv_color,
        cv.Optional(CONF_PRESSED_OPACITY, default="12%"): cv.percentage,
        cv.Optional(
            CONF_ENTER_DURATION, default="80ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_EXIT_DURATION, default="120ms"
        ): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)

DIRECT_STATE_LAYER_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MaterialDirectStateLayer),
        cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
        cv.Required(CONF_WIDGETS): cv.All(
            cv.ensure_list(cv.use_id(lv_obj_t)), cv.Length(min=1)
        ),
        cv.Optional(CONF_PRESSED_OPACITY, default="12%"): cv.percentage,
    }
).extend(cv.COMPONENT_SCHEMA)

DIRECT_MARQUEE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MaterialDirectMarquee),
        cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
        cv.Required(CONF_LABEL): cv.use_id(lv_obj_t),
        cv.Required(CONF_VIEWPORT): cv.use_id(lv_obj_t),
    }
).extend(cv.COMPONENT_SCHEMA)

DIRECT_SPINNER_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MaterialDirectSpinner),
        cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
        cv.Required(CONF_WIDGET): cv.use_id(lv_obj_t),
        cv.Optional(CONF_BACKGROUND_COLOR, default=0x000000): lv_color,
        cv.Optional(CONF_TRACK_COLOR, default=0x34283E): lv_color,
        cv.Optional(CONF_INDICATOR_COLOR, default=0xE8DEF8): lv_color,
        cv.Optional(CONF_THICKNESS, default=7): cv.int_range(min=1, max=64),
        cv.Optional(CONF_ARC_LENGTH, default=80): cv.int_range(min=1, max=359),
        cv.Optional(CONF_FRAME_RATE, default=30): cv.int_range(min=10, max=60),
        cv.Optional(
            CONF_SPIN_TIME, default="900ms"
        ): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)

DIRECT_VOLUME_OVERLAY_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MaterialDirectVolumeOverlay),
        cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
        cv.Required(CONF_ARC): cv.use_id(lv_obj_t),
        cv.Required(CONF_KNOB): cv.use_id(lv_obj_t),
        cv.Required(CONF_LABEL): cv.use_id(lv_obj_t),
        cv.Optional(CONF_ACTIVATION_WIDGET): cv.use_id(lv_obj_t),
        cv.Optional(CONF_SCRIM_OPACITY, default="72%"): cv.percentage,
        cv.Optional(CONF_SCENE_CONTROLLERS, default=list): cv.All(
            cv.ensure_list(cv.use_id(DirectSceneController)),
            cv.Length(max=4),
        ),
    }
).extend(cv.COMPONENT_SCHEMA)

WAVY_PROGRESS_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MaterialWavyProgress),
        cv.Required(CONF_WIDGET): cv.use_id(lv_obj_t),
    }
).extend(cv.COMPONENT_SCHEMA)

VOICE_ASSISTANT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MaterialVoiceAssistant),
        cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
        cv.Required(CONF_ROOT): cv.use_id(lv_obj_t),
        cv.Required(CONF_STATUS_LABEL): cv.use_id(lv_obj_t),
        cv.Required(CONF_USER_LABEL): cv.use_id(lv_obj_t),
        cv.Required(CONF_ASSISTANT_LABEL): cv.use_id(lv_obj_t),
        cv.Required(CONF_WAVEFORM): cv.use_id(lv_obj_t),
        cv.Optional(CONF_FRAME_RATE, default=30): cv.int_range(min=15, max=60),
        cv.Optional(CONF_PRIMARY_COLOR, default=0xE4C2FF): lv_color,
        cv.Optional(CONF_SECONDARY_COLOR, default=0xD9C2FF): lv_color,
        cv.Optional(CONF_TERTIARY_COLOR, default=0x735E9A): lv_color,
        cv.Optional(CONF_GRADIENT_BOTTOM_COLOR, default=0x2D2136): lv_color,
        cv.Optional(CONF_GRADIENT_START, default="62.5%"): cv.percentage,
    }
).extend(cv.COMPONENT_SCHEMA)

PRESSED_STYLE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MaterialPressedStyle),
        cv.Required(CONF_WIDGETS): cv.All(
            cv.ensure_list(cv.use_id(lv_obj_t)), cv.Length(min=1)
        ),
        cv.Optional(CONF_PRESSED_OPACITY, default="12%"): cv.percentage,
    }
).extend(cv.COMPONENT_SCHEMA)

PAGE_INDICATOR_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MaterialPageIndicator),
        cv.Required(CONF_CONTAINER): cv.use_id(lv_obj_t),
        cv.Required(CONF_COUNT): cv.int_range(min=1, max=32),
        cv.Optional(CONF_INITIAL_PAGE, default=0): cv.int_range(min=0),
        cv.Optional(CONF_ACTIVE_SIZE, default=24): cv.int_range(min=1, max=255),
        cv.Optional(CONF_INACTIVE_SIZE, default=8): cv.int_range(min=1, max=255),
        cv.Optional(CONF_THICKNESS, default=8): cv.int_range(min=1, max=255),
        cv.Optional(CONF_GAP, default=8): cv.int_range(min=0, max=255),
        cv.Optional(CONF_ACTIVE_COLOR, default=0xFFFFFF): lv_color,
        cv.Optional(CONF_INACTIVE_COLOR, default=0x777777): lv_color,
        cv.Optional(
            CONF_TRANSITION_DURATION, default="160ms"
        ): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)

TILE_BINDING_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_SLOT): cv.int_range(min=0, max=255),
        cv.Required(CONF_WIDGET): cv.use_id(lv_obj_t),
        cv.Required(CONF_ICON_LABEL): cv.use_id(lv_obj_t),
        cv.Required(CONF_TITLE_LABEL): cv.use_id(lv_obj_t),
        cv.Required(CONF_SUBTITLE_LABEL): cv.use_id(lv_obj_t),
        cv.Optional(CONF_ALWAYS_HIDDEN, default=False): cv.boolean,
    }
)

TILE_SURFACE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MaterialTileSurface),
        cv.Required(CONF_TILES): cv.All(
            cv.ensure_list(TILE_BINDING_SCHEMA), cv.Length(min=1, max=32)
        ),
        cv.Optional(CONF_ON_PRESS): automation.validate_automation({}),
    }
).extend(cv.COMPONENT_SCHEMA)

NOTIFICATION_OVERLAY_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MaterialNotificationOverlay),
        cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
        cv.Required(CONF_ROOT): cv.use_id(lv_obj_t),
        cv.Required(CONF_CONTAINER): cv.use_id(lv_obj_t),
        cv.Required(CONF_ICON_LABEL): cv.use_id(lv_obj_t),
        cv.Required(CONF_TITLE_LABEL): cv.use_id(lv_obj_t),
        cv.Required(CONF_SUBTITLE_LABEL): cv.use_id(lv_obj_t),
        cv.Optional(CONF_SCRIM_OPACITY, default="68%"): cv.percentage,
        cv.Optional(
            CONF_DURATION, default="6s"
        ): cv.positive_time_period_milliseconds,
    }
).extend(cv.COMPONENT_SCHEMA)

WEATHER_ANIMATION_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_KEY): cv.string_strict,
        cv.Required(CONF_WIDGET): cv.use_id(lv_obj_t),
    }
)

WEATHER_PRESENTER_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MaterialWeatherPresenter),
        cv.Required(CONF_ROOT): cv.use_id(lv_obj_t),
        cv.Required(CONF_TEMPERATURE_LABEL): cv.use_id(lv_obj_t),
        cv.Required(CONF_CONDITION_LABEL): cv.use_id(lv_obj_t),
        cv.Required(CONF_DETAIL_LABEL): cv.use_id(lv_obj_t),
        cv.Optional(CONF_INTERACTION_WIDGET): cv.use_id(lv_obj_t),
        cv.Optional(CONF_INTERACTION_STATE_LAYER): cv.use_id(MaterialDirectStateLayer),
        cv.Optional(CONF_ANIMATION_LAYER): cv.use_id(lv_obj_t),
        cv.Optional(CONF_SNAPSHOT_PAGE): cv.use_id(lv_obj_t),
        cv.Optional(CONF_SNAPSHOT_REGION): cv.use_id(lv_obj_t),
        cv.Optional(
            CONF_SNAPSHOT_REFRESH_DELAY, default="120ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_RESUME_DELAY, default="300ms"
        ): cv.positive_time_period_milliseconds,
        cv.Required(CONF_ANIMATIONS): cv.All(
            cv.ensure_list(WEATHER_ANIMATION_SCHEMA), cv.Length(min=1, max=16)
        ),
        cv.Optional(CONF_ON_ANIMATION_COMPLETE): automation.validate_automation({}),
    }
).extend(cv.COMPONENT_SCHEMA)


def _validate_config(config):
    if (
        not config.get(CONF_STATE_LAYERS)
        and not config.get(CONF_DIRECT_STATE_LAYERS)
        and not config.get(CONF_DIRECT_MARQUEES)
        and not config.get(CONF_DIRECT_SPINNERS)
        and not config.get(CONF_DIRECT_VOLUME_OVERLAYS)
        and not config.get(CONF_WAVY_PROGRESS)
        and not config.get(CONF_VOICE_ASSISTANTS)
        and not config.get(CONF_PRESSED_STYLES)
        and not config.get(CONF_PAGE_INDICATORS)
        and not config.get(CONF_TILE_SURFACES)
        and not config.get(CONF_NOTIFICATION_OVERLAYS)
        and not config.get(CONF_WEATHER_PRESENTERS)
    ):
        raise cv.Invalid(
            f"At least one of {CONF_STATE_LAYERS}, {CONF_DIRECT_STATE_LAYERS}, "
            f"{CONF_DIRECT_MARQUEES}, {CONF_DIRECT_SPINNERS}, "
            f"{CONF_DIRECT_VOLUME_OVERLAYS}, "
            f"{CONF_WAVY_PROGRESS}, {CONF_VOICE_ASSISTANTS}, "
            f"{CONF_PRESSED_STYLES}, "
            f"{CONF_PAGE_INDICATORS}, {CONF_TILE_SURFACES}, "
            f"{CONF_NOTIFICATION_OVERLAYS}, or {CONF_WEATHER_PRESENTERS} is required"
        )
    for indicator in config.get(CONF_PAGE_INDICATORS, []):
        if indicator[CONF_INITIAL_PAGE] >= indicator[CONF_COUNT]:
            raise cv.Invalid(f"{CONF_INITIAL_PAGE} must be lower than {CONF_COUNT}")
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Optional(CONF_STATE_LAYERS, default=list): cv.ensure_list(
                STATE_LAYER_SCHEMA
            ),
            cv.Optional(CONF_DIRECT_STATE_LAYERS, default=list): cv.ensure_list(
                DIRECT_STATE_LAYER_SCHEMA
            ),
            cv.Optional(CONF_DIRECT_MARQUEES, default=list): cv.ensure_list(
                DIRECT_MARQUEE_SCHEMA
            ),
            cv.Optional(CONF_DIRECT_SPINNERS, default=list): cv.ensure_list(
                DIRECT_SPINNER_SCHEMA
            ),
            cv.Optional(CONF_DIRECT_VOLUME_OVERLAYS, default=list): cv.ensure_list(
                DIRECT_VOLUME_OVERLAY_SCHEMA
            ),
            cv.Optional(CONF_WAVY_PROGRESS, default=list): cv.ensure_list(
                WAVY_PROGRESS_SCHEMA
            ),
            cv.Optional(CONF_VOICE_ASSISTANTS, default=list): cv.ensure_list(
                VOICE_ASSISTANT_SCHEMA
            ),
            cv.Optional(CONF_PRESSED_STYLES, default=list): cv.ensure_list(
                PRESSED_STYLE_SCHEMA
            ),
            cv.Optional(CONF_PAGE_INDICATORS, default=list): cv.ensure_list(
                PAGE_INDICATOR_SCHEMA
            ),
            cv.Optional(CONF_TILE_SURFACES, default=list): cv.ensure_list(
                TILE_SURFACE_SCHEMA
            ),
            cv.Optional(CONF_NOTIFICATION_OVERLAYS, default=list): cv.ensure_list(
                NOTIFICATION_OVERLAY_SCHEMA
            ),
            cv.Optional(CONF_WEATHER_PRESENTERS, default=list): cv.ensure_list(
                WEATHER_PRESENTER_SCHEMA
            ),
        }
    ),
    _validate_config,
)


async def to_code(config):
    weather_presenter_bindings = []
    for conf in config[CONF_WEATHER_PRESENTERS]:
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)
        for automation_conf in conf.get(CONF_ON_ANIMATION_COMPLETE, []):
            await automation.build_callback_automation(
                var,
                "add_on_animation_complete_callback",
                [],
                automation_conf,
            )
        root = (await get_widgets(conf, CONF_ROOT))[0]
        temperature = (await get_widgets(conf, CONF_TEMPERATURE_LABEL))[0]
        condition = (await get_widgets(conf, CONF_CONDITION_LABEL))[0]
        detail = (await get_widgets(conf, CONF_DETAIL_LABEL))[0]
        interaction_widget = None
        if CONF_INTERACTION_WIDGET in conf:
            interaction_widget = (
                await get_widgets(conf, CONF_INTERACTION_WIDGET)
            )[0]
        interaction_state_layer_id = conf.get(CONF_INTERACTION_STATE_LAYER)
        animation_layer = None
        if CONF_ANIMATION_LAYER in conf:
            animation_layer = (await get_widgets(conf, CONF_ANIMATION_LAYER))[0]
        snapshot_page = None
        snapshot_region = None
        if CONF_SNAPSHOT_PAGE in conf:
            snapshot_page = (await get_widgets(conf, CONF_SNAPSHOT_PAGE))[0]
        if CONF_SNAPSHOT_REGION in conf:
            snapshot_region = (await get_widgets(conf, CONF_SNAPSHOT_REGION))[0]
        weather_presenter_bindings.append(
            (
                var,
                root,
                temperature,
                condition,
                detail,
                interaction_widget,
                interaction_state_layer_id,
                animation_layer,
                snapshot_page,
                snapshot_region,
                conf[CONF_SNAPSHOT_REFRESH_DELAY],
                conf[CONF_RESUME_DELAY],
                conf[CONF_ANIMATIONS],
            )
        )

    tile_surface_bindings = []
    for conf in config[CONF_TILE_SURFACES]:
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)
        for automation_conf in conf.get(CONF_ON_PRESS, []):
            await automation.build_callback_automation(
                var,
                "add_on_press_callback",
                [(cg.uint8, "slot")],
                automation_conf,
            )
        tile_surface_bindings.append((var, conf[CONF_TILES]))

    notification_overlay_bindings = []
    for conf in config[CONF_NOTIFICATION_OVERLAYS]:
        lvgl = await cg.get_variable(conf[CONF_LVGL_ID])
        var = cg.new_Pvariable(conf[CONF_ID], lvgl)
        await cg.register_component(var, conf)
        cg.add(var.set_scrim_opacity(round(conf[CONF_SCRIM_OPACITY] * 255)))
        cg.add(var.set_default_duration(conf[CONF_DURATION].total_milliseconds))
        root = (await get_widgets(conf, CONF_ROOT))[0]
        panel = (await get_widgets(conf, CONF_CONTAINER))[0]
        icon = (await get_widgets(conf, CONF_ICON_LABEL))[0]
        title = (await get_widgets(conf, CONF_TITLE_LABEL))[0]
        message = (await get_widgets(conf, CONF_SUBTITLE_LABEL))[0]
        notification_overlay_bindings.append(
            (var, root, panel, icon, title, message)
        )

    voice_assistant_bindings = []
    for conf in config[CONF_VOICE_ASSISTANTS]:
        lvgl = await cg.get_variable(conf[CONF_LVGL_ID])
        var = cg.new_Pvariable(conf[CONF_ID], lvgl)
        await cg.register_component(var, conf)
        cg.add(var.set_frame_interval(max(1, 1000 // conf[CONF_FRAME_RATE])))
        cg.add(
            var.set_wave_colors(
                await lv_color.process(conf[CONF_PRIMARY_COLOR]),
                await lv_color.process(conf[CONF_SECONDARY_COLOR]),
                await lv_color.process(conf[CONF_TERTIARY_COLOR]),
            )
        )
        cg.add(
            var.set_gradient(
                await lv_color.process(conf[CONF_GRADIENT_BOTTOM_COLOR]),
                conf[CONF_GRADIENT_START],
            )
        )
        root = (await get_widgets(conf, CONF_ROOT))[0]
        status_label = (await get_widgets(conf, CONF_STATUS_LABEL))[0]
        user_label = (await get_widgets(conf, CONF_USER_LABEL))[0]
        assistant_label = (await get_widgets(conf, CONF_ASSISTANT_LABEL))[0]
        waveform = (await get_widgets(conf, CONF_WAVEFORM))[0]
        voice_assistant_bindings.append(
            (var, root, status_label, user_label, assistant_label, waveform)
        )

    wavy_progress_bindings = []
    for conf in config[CONF_WAVY_PROGRESS]:
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)
        widget = (await get_widgets(conf, CONF_WIDGET))[0]
        wavy_progress_bindings.append((var, widget))

    direct_volume_overlay_bindings = []
    for conf in config[CONF_DIRECT_VOLUME_OVERLAYS]:
        lvgl = await cg.get_variable(conf[CONF_LVGL_ID])
        var = cg.new_Pvariable(conf[CONF_ID], lvgl)
        await cg.register_component(var, conf)
        cg.add(var.set_scrim_opacity(round(conf[CONF_SCRIM_OPACITY] * 255)))
        arc = (await get_widgets(conf, CONF_ARC))[0]
        knob = (await get_widgets(conf, CONF_KNOB))[0]
        label = (await get_widgets(conf, CONF_LABEL))[0]
        activation_widget = None
        if CONF_ACTIVATION_WIDGET in conf:
            activation_widget = (await get_widgets(conf, CONF_ACTIVATION_WIDGET))[0]
        for controller_id in conf[CONF_SCENE_CONTROLLERS]:
            controller = await cg.get_variable(controller_id)
            cg.add(var.add_scene_controller(controller))
        direct_volume_overlay_bindings.append(
            (var, arc, knob, label, activation_widget)
        )

    direct_marquee_bindings = []
    for conf in config[CONF_DIRECT_MARQUEES]:
        lvgl = await cg.get_variable(conf[CONF_LVGL_ID])
        var = cg.new_Pvariable(conf[CONF_ID], lvgl)
        await cg.register_component(var, conf)
        label = (await get_widgets(conf, CONF_LABEL))[0]
        viewport = (await get_widgets(conf, CONF_VIEWPORT))[0]
        direct_marquee_bindings.append((var, label, viewport))

    direct_spinner_bindings = []
    for conf in config[CONF_DIRECT_SPINNERS]:
        lvgl = await cg.get_variable(conf[CONF_LVGL_ID])
        var = cg.new_Pvariable(conf[CONF_ID], lvgl)
        await cg.register_component(var, conf)
        cg.add(
            var.set_colors(
                await lv_color.process(conf[CONF_BACKGROUND_COLOR]),
                await lv_color.process(conf[CONF_TRACK_COLOR]),
                await lv_color.process(conf[CONF_INDICATOR_COLOR]),
            )
        )
        cg.add(var.set_geometry(conf[CONF_THICKNESS], conf[CONF_ARC_LENGTH]))
        cg.add(
            var.set_timing(
                max(1, 1000 // conf[CONF_FRAME_RATE]),
                conf[CONF_SPIN_TIME].total_milliseconds,
            )
        )
        widget = (await get_widgets(conf, CONF_WIDGET))[0]
        direct_spinner_bindings.append((var, widget))

    pressed_style_bindings = []
    for conf in config[CONF_PRESSED_STYLES]:
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)
        cg.add(var.set_pressed_opacity(round(conf[CONF_PRESSED_OPACITY] * 255)))
        widgets = await get_widgets(
            [{CONF_ID: widget_id} for widget_id in conf[CONF_WIDGETS]]
        )
        pressed_style_bindings.append((var, widgets))

    direct_state_layer_bindings = []
    for conf in config[CONF_DIRECT_STATE_LAYERS]:
        lvgl = await cg.get_variable(conf[CONF_LVGL_ID])
        var = cg.new_Pvariable(conf[CONF_ID], lvgl)
        await cg.register_component(var, conf)
        cg.add(var.set_pressed_opacity(round(conf[CONF_PRESSED_OPACITY] * 255)))
        widgets = await get_widgets(
            [{CONF_ID: widget_id} for widget_id in conf[CONF_WIDGETS]]
        )
        direct_state_layer_bindings.append((var, widgets))

    state_layer_bindings = []
    for conf in config[CONF_STATE_LAYERS]:
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)
        cg.add(var.set_color(await lv_color.process(conf[CONF_COLOR])))
        cg.add(var.set_pressed_opacity(round(conf[CONF_PRESSED_OPACITY] * 255)))
        cg.add(
            var.set_durations(
                conf[CONF_ENTER_DURATION].total_milliseconds,
                conf[CONF_EXIT_DURATION].total_milliseconds,
            )
        )
        widget = (await get_widgets(conf, CONF_WIDGET))[0]
        state_layer_bindings.append((var, widget))

    page_indicator_bindings = []
    for conf in config[CONF_PAGE_INDICATORS]:
        var = cg.new_Pvariable(conf[CONF_ID])
        await cg.register_component(var, conf)
        cg.add(var.set_count(conf[CONF_COUNT]))
        cg.add(var.set_initial_page(conf[CONF_INITIAL_PAGE]))
        cg.add(
            var.set_geometry(
                conf[CONF_ACTIVE_SIZE],
                conf[CONF_INACTIVE_SIZE],
                conf[CONF_THICKNESS],
                conf[CONF_GAP],
            )
        )
        cg.add(
            var.set_colors(
                await lv_color.process(conf[CONF_ACTIVE_COLOR]),
                await lv_color.process(conf[CONF_INACTIVE_COLOR]),
            )
        )
        cg.add(
            var.set_transition_duration(
                conf[CONF_TRANSITION_DURATION].total_milliseconds
            )
        )
        container = (await get_widgets(conf, CONF_CONTAINER))[0]
        page_indicator_bindings.append((var, container))

    await wait_for_widgets()
    async with LvContext() as ctx:
        for (
            var,
            root,
            temperature,
            condition,
            detail,
            interaction_widget,
            interaction_state_layer_id,
            animation_layer,
            snapshot_page,
            snapshot_region,
            snapshot_refresh_delay,
            resume_delay,
            animations,
        ) in weather_presenter_bindings:
            ctx.add(var.set_root(root.obj))
            ctx.add(var.set_temperature_label(temperature.obj))
            ctx.add(var.set_condition_label(condition.obj))
            ctx.add(var.set_detail_label(detail.obj))
            if interaction_widget is not None:
                ctx.add(var.set_interaction_widget(interaction_widget.obj))
            if interaction_state_layer_id is not None:
                interaction_state_layer = await cg.get_variable(
                    interaction_state_layer_id
                )
                ctx.add(var.set_interaction_state_layer(interaction_state_layer))
            if animation_layer is not None:
                ctx.add(var.set_animation_layer(animation_layer.obj))
            if snapshot_page is not None:
                ctx.add(var.set_snapshot_page(snapshot_page.obj))
            if snapshot_region is not None:
                ctx.add(var.set_snapshot_region(snapshot_region.obj))
            ctx.add(
                var.set_snapshot_refresh_delay(
                    snapshot_refresh_delay.total_milliseconds
                )
            )
            ctx.add(var.set_resume_delay(resume_delay.total_milliseconds))
            for animation in animations:
                widget = (await get_widgets(animation, CONF_WIDGET))[0]
                ctx.add(var.add_animation(animation[CONF_KEY], widget.obj))
        for var, bindings in tile_surface_bindings:
            for binding in bindings:
                widget = (await get_widgets(binding, CONF_WIDGET))[0]
                icon = (await get_widgets(binding, CONF_ICON_LABEL))[0]
                title = (await get_widgets(binding, CONF_TITLE_LABEL))[0]
                subtitle = (await get_widgets(binding, CONF_SUBTITLE_LABEL))[0]
                ctx.add(
                    var.add_tile(
                        binding[CONF_SLOT],
                        widget.obj,
                        icon.obj,
                        title.obj,
                        subtitle.obj,
                        binding[CONF_ALWAYS_HIDDEN],
                    )
                )
        for var, root, panel, icon, title, message in notification_overlay_bindings:
            ctx.add(var.set_root(root.obj))
            ctx.add(var.set_panel(panel.obj))
            ctx.add(var.set_icon(icon.obj))
            ctx.add(var.set_title(title.obj))
            ctx.add(var.set_message(message.obj))
        for (
            var,
            root,
            status_label,
            user_label,
            assistant_label,
            waveform,
        ) in voice_assistant_bindings:
            ctx.add(var.set_root(root.obj))
            ctx.add(var.set_status_label(status_label.obj))
            ctx.add(var.set_user_label(user_label.obj))
            ctx.add(var.set_assistant_label(assistant_label.obj))
            ctx.add(var.set_waveform(waveform.obj))
        for var, widget in wavy_progress_bindings:
            ctx.add(var.set_widget(widget.obj))
        for var, arc, knob, label, activation_widget in direct_volume_overlay_bindings:
            ctx.add(var.set_arc(arc.obj))
            ctx.add(var.set_knob(knob.obj))
            ctx.add(var.set_label(label.obj))
            if activation_widget is not None:
                ctx.add(var.set_activation_widget(activation_widget.obj))
        for var, label, viewport in direct_marquee_bindings:
            ctx.add(var.set_label(label.obj))
            ctx.add(var.set_viewport(viewport.obj))
        for var, widget in direct_spinner_bindings:
            ctx.add(var.set_widget(widget.obj))
        for var, widgets in pressed_style_bindings:
            for widget in widgets:
                ctx.add(var.add_target(widget.obj))
        for var, widgets in direct_state_layer_bindings:
            for widget in widgets:
                ctx.add(var.add_target(widget.obj))
        for var, widget in state_layer_bindings:
            ctx.add(var.set_target(widget.obj))
        for var, container in page_indicator_bindings:
            ctx.add(var.set_container(container.obj))


PAGE_INDICATOR_SET_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(MaterialPageIndicator),
        cv.Required(CONF_VALUE): cv.templatable(cv.int_range(min=0, max=31)),
    }
)


@automation.register_action(
    "lvgl_material.page_indicator.set",
    MaterialPageIndicatorSetAction,
    PAGE_INDICATOR_SET_SCHEMA,
    synchronous=True,
)
async def page_indicator_set_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    value = await cg.templatable(config[CONF_VALUE], args, cg.uint16)
    cg.add(var.set_value(value))
    return var


TILE_SURFACE_CONFIGURE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(MaterialTileSurface),
        cv.Required(CONF_PAYLOAD): cv.templatable(cv.string_strict),
    }
)


@automation.register_action(
    "lvgl_material.tiles.configure",
    MaterialTileSurfaceConfigureAction,
    TILE_SURFACE_CONFIGURE_SCHEMA,
    synchronous=True,
)
async def tile_surface_configure_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    payload = await cg.templatable(config[CONF_PAYLOAD], args, cg.std_string)
    cg.add(var.set_payload(payload))
    return var


NOTIFICATION_SHOW_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(MaterialNotificationOverlay),
        cv.Required(CONF_TITLE): cv.templatable(cv.string_strict),
        cv.Required(CONF_MESSAGE): cv.templatable(cv.string_strict),
        cv.Optional(CONF_ICON, default=""): cv.templatable(cv.string_strict),
        cv.Optional(CONF_DURATION, default=0): cv.templatable(
            cv.int_range(min=0, max=600000)
        ),
    }
)


@automation.register_action(
    "lvgl_material.notification.show",
    MaterialNotificationShowAction,
    NOTIFICATION_SHOW_SCHEMA,
    synchronous=True,
)
async def notification_show_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    title = await cg.templatable(config[CONF_TITLE], args, cg.std_string)
    message = await cg.templatable(config[CONF_MESSAGE], args, cg.std_string)
    icon = await cg.templatable(config[CONF_ICON], args, cg.std_string)
    duration = await cg.templatable(config[CONF_DURATION], args, cg.uint32)
    cg.add(var.set_title(title))
    cg.add(var.set_message(message))
    cg.add(var.set_icon(icon))
    cg.add(var.set_duration(duration))
    return var


NOTIFICATION_DISMISS_SCHEMA = cv.Schema(
    {cv.GenerateID(): cv.use_id(MaterialNotificationOverlay)}
)


@automation.register_action(
    "lvgl_material.notification.dismiss",
    MaterialNotificationDismissAction,
    NOTIFICATION_DISMISS_SCHEMA,
    synchronous=True,
)
async def notification_dismiss_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


WEATHER_UPDATE_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(MaterialWeatherPresenter),
        cv.Required(CONF_CONDITION): cv.templatable(cv.string_strict),
        cv.Required(CONF_IS_DAY): cv.templatable(cv.boolean),
        cv.Required(CONF_TEMPERATURE): cv.templatable(cv.float_),
        cv.Required(CONF_APPARENT_TEMPERATURE): cv.templatable(cv.float_),
        cv.Required(CONF_HUMIDITY): cv.templatable(cv.float_),
    }
)


@automation.register_action(
    "lvgl_material.weather.update",
    MaterialWeatherUpdateAction,
    WEATHER_UPDATE_SCHEMA,
    synchronous=True,
)
async def weather_update_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    condition = await cg.templatable(config[CONF_CONDITION], args, cg.std_string)
    is_day = await cg.templatable(config[CONF_IS_DAY], args, cg.bool_)
    temperature = await cg.templatable(config[CONF_TEMPERATURE], args, cg.float_)
    apparent_temperature = await cg.templatable(
        config[CONF_APPARENT_TEMPERATURE], args, cg.float_
    )
    humidity = await cg.templatable(config[CONF_HUMIDITY], args, cg.float_)
    cg.add(var.set_condition(condition))
    cg.add(var.set_is_day(is_day))
    cg.add(var.set_temperature(temperature))
    cg.add(var.set_apparent_temperature(apparent_temperature))
    cg.add(var.set_humidity(humidity))
    return var


@automation.register_action(
    "lvgl_material.weather.suspend",
    MaterialWeatherSuspendAction,
    cv.maybe_simple_value(
        cv.Schema({cv.Required(CONF_ID): cv.use_id(MaterialWeatherPresenter)}),
        key=CONF_ID,
    ),
    synchronous=True,
)
async def weather_suspend_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


@automation.register_action(
    "lvgl_material.weather.resume",
    MaterialWeatherResumeAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(MaterialWeatherPresenter),
            cv.Required(CONF_VALUE): cv.templatable(cv.boolean),
        }
    ),
    synchronous=True,
)
async def weather_resume_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    value = await cg.templatable(config[CONF_VALUE], args, cg.bool_)
    cg.add(var.set_page_visible(value))
    return var


DIRECT_SPINNER_ACTION_SCHEMA = cv.maybe_simple_value(
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(MaterialDirectSpinner),
            cv.Optional(CONF_CLEAR, default=True): cv.boolean,
        }
    ),
    key=CONF_ID,
)


@automation.register_action(
    "lvgl_material.spinner.start",
    MaterialDirectSpinnerStartAction,
    DIRECT_SPINNER_ACTION_SCHEMA,
    synchronous=True,
)
async def direct_spinner_start_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent)


@automation.register_action(
    "lvgl_material.spinner.stop",
    MaterialDirectSpinnerStopAction,
    DIRECT_SPINNER_ACTION_SCHEMA,
    synchronous=True,
)
async def direct_spinner_stop_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, parent, config[CONF_CLEAR])
