from esphome import automation
import esphome.codegen as cg
from esphome.components.lvgl.defines import CONF_LVGL_ID, CONF_WIDGETS
from esphome.components.lvgl.lv_validation import lv_color
from esphome.components.lvgl.lvcode import LvContext, LvglComponent
from esphome.components.lvgl.types import DirectSceneController, lv_obj_t
from esphome.components.lvgl.widgets import get_widgets, wait_for_widgets
import esphome.config_validation as cv
from esphome.const import CONF_COLOR, CONF_COUNT, CONF_ID, CONF_VALUE

CODEOWNERS = ["@kyvaith"]
DEPENDENCIES = ["lvgl"]

CONF_ACTIVE_COLOR = "active_color"
CONF_ACTIVE_SIZE = "active_size"
CONF_ACTIVATION_WIDGET = "activation_widget"
CONF_ARC = "arc"
CONF_CONTAINER = "container"
CONF_DIRECT_MARQUEES = "direct_marquees"
CONF_DIRECT_STATE_LAYERS = "direct_state_layers"
CONF_DIRECT_VOLUME_OVERLAYS = "direct_volume_overlays"
CONF_ENTER_DURATION = "enter_duration"
CONF_EXIT_DURATION = "exit_duration"
CONF_GAP = "gap"
CONF_FRAME_RATE = "frame_rate"
CONF_GRADIENT_BOTTOM_COLOR = "gradient_bottom_color"
CONF_GRADIENT_START = "gradient_start"
CONF_INACTIVE_COLOR = "inactive_color"
CONF_INACTIVE_SIZE = "inactive_size"
CONF_INITIAL_PAGE = "initial_page"
CONF_KNOB = "knob"
CONF_PAGE_INDICATORS = "page_indicators"
CONF_PRESSED_OPACITY = "pressed_opacity"
CONF_PRESSED_STYLES = "pressed_styles"
CONF_PRIMARY_COLOR = "primary_color"
CONF_SCRIM_OPACITY = "scrim_opacity"
CONF_SECONDARY_COLOR = "secondary_color"
CONF_SCENE_CONTROLLERS = "scene_controllers"
CONF_STATE_LAYERS = "state_layers"
CONF_THICKNESS = "thickness"
CONF_TRANSITION_DURATION = "transition_duration"
CONF_TERTIARY_COLOR = "tertiary_color"
CONF_LABEL = "label"
CONF_ASSISTANT_LABEL = "assistant_label"
CONF_ROOT = "root"
CONF_STATUS_LABEL = "status_label"
CONF_USER_LABEL = "user_label"
CONF_VIEWPORT = "viewport"
CONF_VOICE_ASSISTANTS = "voice_assistants"
CONF_WAVEFORM = "waveform"
CONF_WAVY_PROGRESS = "wavy_progress"
CONF_WIDGET = "widget"

lvgl_material_ns = cg.esphome_ns.namespace("lvgl_material")
MaterialStateLayer = lvgl_material_ns.class_("MaterialStateLayer", cg.Component)
MaterialDirectStateLayer = lvgl_material_ns.class_(
    "MaterialDirectStateLayer", cg.Component
)
MaterialDirectMarquee = lvgl_material_ns.class_("MaterialDirectMarquee", cg.Component)
MaterialDirectVolumeOverlay = lvgl_material_ns.class_(
    "MaterialDirectVolumeOverlay", cg.Component
)
MaterialWavyProgress = lvgl_material_ns.class_("MaterialWavyProgress", cg.Component)
MaterialVoiceAssistant = lvgl_material_ns.class_(
    "MaterialVoiceAssistant", cg.Component
)
MaterialPressedStyle = lvgl_material_ns.class_("MaterialPressedStyle", cg.Component)
MaterialPageIndicator = lvgl_material_ns.class_("MaterialPageIndicator", cg.Component)
MaterialPageIndicatorSetAction = lvgl_material_ns.class_(
    "MaterialPageIndicatorSetAction",
    automation.Action,
    cg.Parented.template(MaterialPageIndicator),
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


def _validate_config(config):
    if (
        not config.get(CONF_STATE_LAYERS)
        and not config.get(CONF_DIRECT_STATE_LAYERS)
        and not config.get(CONF_DIRECT_MARQUEES)
        and not config.get(CONF_DIRECT_VOLUME_OVERLAYS)
        and not config.get(CONF_WAVY_PROGRESS)
        and not config.get(CONF_VOICE_ASSISTANTS)
        and not config.get(CONF_PRESSED_STYLES)
        and not config.get(CONF_PAGE_INDICATORS)
    ):
        raise cv.Invalid(
            f"At least one of {CONF_STATE_LAYERS}, {CONF_DIRECT_STATE_LAYERS}, "
            f"{CONF_DIRECT_MARQUEES}, {CONF_DIRECT_VOLUME_OVERLAYS}, "
            f"{CONF_WAVY_PROGRESS}, {CONF_VOICE_ASSISTANTS}, "
            f"{CONF_PRESSED_STYLES}, "
            f"or {CONF_PAGE_INDICATORS} is required"
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
        }
    ),
    _validate_config,
)


async def to_code(config):
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
