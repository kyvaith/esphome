from esphome import automation
import esphome.codegen as cg
from esphome.components import esp32
from esphome.components.esp32 import VARIANT_ESP32P4, only_on_variant
import esphome.config_validation as cv
from esphome.const import CONF_ID

DEPENDENCIES = ["esp32"]
CODEOWNERS = ["@kyvaith"]

task_runtime_profiler_ns = cg.esphome_ns.namespace("task_runtime_profiler")
TaskRuntimeProfiler = task_runtime_profiler_ns.class_(
    "TaskRuntimeProfiler", cg.Component
)
TaskRuntimeProfilerLogAction = task_runtime_profiler_ns.class_(
    "TaskRuntimeProfilerLogAction",
    automation.Action,
    cg.Parented.template(TaskRuntimeProfiler),
)

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(TaskRuntimeProfiler),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.only_on_esp32,
    cv.only_with_framework("esp-idf"),
    only_on_variant(supported=[VARIANT_ESP32P4]),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    esp32.add_idf_sdkconfig_option("CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS", True)


TASK_RUNTIME_PROFILER_LOG_SCHEMA = automation.maybe_simple_id(
    {
        cv.GenerateID(): cv.use_id(TaskRuntimeProfiler),
    }
)


@automation.register_action(
    "task_runtime_profiler.log",
    TaskRuntimeProfilerLogAction,
    TASK_RUNTIME_PROFILER_LOG_SCHEMA,
    synchronous=True,
)
async def task_runtime_profiler_log_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
