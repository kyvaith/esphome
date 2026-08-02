from esphome import automation
import esphome.codegen as cg
from esphome.components import esp32, select, socket
from esphome.components.const import CONF_REQUEST_HEADERS
from esphome.components.esp32 import VARIANT_ESP32P4, only_on_variant
from esphome.components.image import CONF_OPAQUE, Image_, add_metadata
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_NAME, CONF_URL, ENTITY_CATEGORY_CONFIG

CODEOWNERS = ["@kyvaith"]
AUTO_LOAD = ["esp32_jpeg", "image", "select"]
DEPENDENCIES = ["esp32", "network"]

CONF_FRAME_INTERVAL = "frame_interval"
CONF_INDEX = "index"
CONF_MAX_FRAME_SIZE = "max_frame_size"
CONF_MAX_HEIGHT = "max_height"
CONF_MAX_RUNTIME_SOURCES = "max_runtime_sources"
CONF_MAX_WIDTH = "max_width"
CONF_NAMES = "names"
CONF_ON_FIRST_FRAME = "on_first_frame"
CONF_ON_SOURCE = "on_source"
CONF_ON_STATE = "on_state"
CONF_RECONNECT_INTERVAL = "reconnect_interval"
CONF_RELEASE_BUFFER_ON_STOP = "release_buffer_on_stop"
CONF_REQUEST_TIMEOUT = "request_timeout"
CONF_SOURCES = "sources"
CONF_SOURCE_SELECT = "source_select"
CONF_TASK_CORE = "task_core"
CONF_TASK_PRIORITY = "task_priority"
CONF_TASK_STACK_SIZE = "task_stack_size"
CONF_URLS = "urls"

network_camera_ns = cg.esphome_ns.namespace("network_camera")
NetworkCamera = network_camera_ns.class_("NetworkCamera", cg.Component, Image_)
NetworkCameraSourceSelect = network_camera_ns.class_(
    "NetworkCameraSourceSelect", select.Select, cg.Parented.template(NetworkCamera)
)
NetworkCameraStartAction = network_camera_ns.class_(
    "NetworkCameraStartAction", automation.Action, cg.Parented.template(NetworkCamera)
)
NetworkCameraStopAction = network_camera_ns.class_(
    "NetworkCameraStopAction", automation.Action, cg.Parented.template(NetworkCamera)
)
NetworkCameraNextAction = network_camera_ns.class_(
    "NetworkCameraNextAction", automation.Action, cg.Parented.template(NetworkCamera)
)
NetworkCameraPreviousAction = network_camera_ns.class_(
    "NetworkCameraPreviousAction",
    automation.Action,
    cg.Parented.template(NetworkCamera),
)
NetworkCameraSelectAction = network_camera_ns.class_(
    "NetworkCameraSelectAction", automation.Action, cg.Parented.template(NetworkCamera)
)
NetworkCameraReplaceSourcesAction = network_camera_ns.class_(
    "NetworkCameraReplaceSourcesAction",
    automation.Action,
    cg.Parented.template(NetworkCamera),
)

SOURCE_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_NAME): cv.string_strict,
        cv.Required(CONF_URL): cv.url,
        cv.Optional(CONF_REQUEST_HEADERS, default={}): cv.Schema(
            {cv.string: cv.string}
        ),
    }
)


def _validate_source_capacity(config):
    if len(config[CONF_SOURCES]) > config[CONF_MAX_RUNTIME_SOURCES]:
        raise cv.Invalid(
            "max_runtime_sources must be at least the number of configured sources"
        )
    if not config[CONF_SOURCES] and CONF_SOURCE_SELECT in config:
        raise cv.Invalid(
            "source_select requires at least one statically configured source"
        )
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(NetworkCamera),
            cv.Optional(CONF_SOURCES, default=[]): cv.ensure_list(SOURCE_SCHEMA),
            cv.Optional(CONF_MAX_FRAME_SIZE, default="512kB"): cv.All(
                cv.validate_bytes,
                cv.int_range(min=32 * 1024, max=4 * 1024 * 1024),
            ),
            cv.Optional(CONF_MAX_WIDTH, default=1920): cv.int_range(min=64, max=4096),
            cv.Optional(CONF_MAX_HEIGHT, default=1080): cv.int_range(min=64, max=4096),
            cv.Optional(CONF_MAX_RUNTIME_SOURCES, default=16): cv.int_range(
                min=1, max=64
            ),
            cv.Optional(CONF_FRAME_INTERVAL, default="67ms"): cv.All(
                cv.positive_time_period_milliseconds,
                cv.Range(
                    min=cv.TimePeriod(milliseconds=16),
                    max=cv.TimePeriod(milliseconds=1000),
                ),
            ),
            cv.Optional(
                CONF_RECONNECT_INTERVAL, default="2s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(
                CONF_REQUEST_TIMEOUT, default="3s"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_RELEASE_BUFFER_ON_STOP, default=True): cv.boolean,
            cv.Optional(CONF_TASK_CORE, default=-1): cv.int_range(min=-1, max=1),
            cv.Optional(CONF_TASK_PRIORITY, default=5): cv.int_range(min=1, max=20),
            cv.Optional(CONF_TASK_STACK_SIZE, default=8192): cv.int_range(
                min=4096, max=32768
            ),
            cv.Optional(CONF_SOURCE_SELECT): select.select_schema(
                NetworkCameraSourceSelect,
                entity_category=ENTITY_CATEGORY_CONFIG,
                icon="mdi:camera-switch",
            ),
            cv.Optional(CONF_ON_FIRST_FRAME): automation.validate_automation({}),
            cv.Optional(CONF_ON_STATE): automation.validate_automation({}),
            cv.Optional(CONF_ON_SOURCE): automation.validate_automation({}),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_source_capacity,
    socket.consume_sockets(1, "network_camera"),
    cv.only_on_esp32,
    cv.only_with_framework("esp-idf"),
    only_on_variant(supported=[VARIANT_ESP32P4]),
)

_CALLBACK_AUTOMATIONS = (
    automation.CallbackAutomation(CONF_ON_FIRST_FRAME, "add_on_first_frame_callback"),
    automation.CallbackAutomation(
        CONF_ON_STATE, "add_on_state_callback", [(cg.std_string, "state")]
    ),
    automation.CallbackAutomation(
        CONF_ON_SOURCE, "add_on_source_callback", [(cg.std_string, "source")]
    ),
)


async def to_code(config):
    add_metadata(
        config[CONF_ID],
        config[CONF_MAX_WIDTH],
        config[CONF_MAX_HEIGHT],
        "RGB",
        CONF_OPAQUE,
    )

    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    for source_index, source in enumerate(config[CONF_SOURCES]):
        cg.add(var.add_source(source[CONF_NAME], source[CONF_URL]))
        for name, value in source[CONF_REQUEST_HEADERS].items():
            cg.add(var.add_source_header(source_index, name, value))

    cg.add(var.set_max_frame_size(config[CONF_MAX_FRAME_SIZE]))
    cg.add(var.set_max_dimensions(config[CONF_MAX_WIDTH], config[CONF_MAX_HEIGHT]))
    cg.add(var.set_max_runtime_sources(config[CONF_MAX_RUNTIME_SOURCES]))
    cg.add(var.set_frame_interval(config[CONF_FRAME_INTERVAL].total_milliseconds))
    cg.add(
        var.set_reconnect_interval(config[CONF_RECONNECT_INTERVAL].total_milliseconds)
    )
    cg.add(var.set_request_timeout(config[CONF_REQUEST_TIMEOUT].total_milliseconds))
    cg.add(var.set_release_buffer_on_stop(config[CONF_RELEASE_BUFFER_ON_STOP]))
    cg.add(var.set_task_core(config[CONF_TASK_CORE]))
    cg.add(var.set_task_priority(config[CONF_TASK_PRIORITY]))
    cg.add(var.set_task_stack_size(config[CONF_TASK_STACK_SIZE]))

    if source_select_config := config.get(CONF_SOURCE_SELECT):
        source_select = await select.new_select(
            source_select_config,
            options=[source[CONF_NAME] for source in config[CONF_SOURCES]],
        )
        await cg.register_parented(source_select, var)
        cg.add(var.set_source_select(source_select))

    await automation.build_callback_automations(var, config, _CALLBACK_AUTOMATIONS)

    cg.add_define("USE_NETWORK_CAMERA")
    esp32.include_builtin_idf_component("esp_http_client")
    esp32.add_idf_sdkconfig_option("CONFIG_MBEDTLS_CERTIFICATE_BUNDLE", True)


ACTION_SCHEMA = automation.maybe_simple_id({cv.GenerateID(): cv.use_id(NetworkCamera)})


@automation.register_action(
    "network_camera.start", NetworkCameraStartAction, ACTION_SCHEMA, synchronous=True
)
@automation.register_action(
    "network_camera.stop", NetworkCameraStopAction, ACTION_SCHEMA, synchronous=True
)
@automation.register_action(
    "network_camera.next", NetworkCameraNextAction, ACTION_SCHEMA, synchronous=True
)
@automation.register_action(
    "network_camera.previous",
    NetworkCameraPreviousAction,
    ACTION_SCHEMA,
    synchronous=True,
)
async def network_camera_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


@automation.register_action(
    "network_camera.select",
    NetworkCameraSelectAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(NetworkCamera),
            cv.Required(CONF_INDEX): cv.templatable(cv.uint16_t),
        }
    ),
    synchronous=True,
)
async def network_camera_select_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    index = await cg.templatable(config[CONF_INDEX], args, cg.uint16)
    cg.add(var.set_index(index))
    return var


@automation.register_action(
    "network_camera.replace_sources",
    NetworkCameraReplaceSourcesAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(NetworkCamera),
            cv.Required(CONF_NAMES): cv.templatable(
                cv.All(cv.ensure_list(cv.string_strict), cv.Length(min=1))
            ),
            cv.Required(CONF_URLS): cv.templatable(
                cv.All(cv.ensure_list(cv.url), cv.Length(min=1))
            ),
        }
    ),
    synchronous=True,
)
async def network_camera_replace_sources_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    names = await cg.templatable(
        config[CONF_NAMES], args, cg.std_vector.template(cg.std_string)
    )
    urls = await cg.templatable(
        config[CONF_URLS], args, cg.std_vector.template(cg.std_string)
    )
    cg.add(var.set_names(names))
    cg.add(var.set_urls(urls))
    return var
