from esphome import automation, codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_CLOSE_DURATION,
    CONF_COMPRESSION,
    CONF_ID,
    CONF_OPEN_DURATION,
    CONF_PAGES,
)

from ..navigation import CONF_APPLICATIONS, CONF_HOME, CONF_PAGE
from ..types import (
    LvglSnapshotCompositor,
    LvglSnapshotStore,
    SnapshotCaptureAction,
    SnapshotCaptureAllAction,
    SnapshotClearAction,
    SnapshotInvalidateAction,
    lv_page_t,
)

CONF_DECODED_SLOTS = "decoded_slots"
CONF_APPLICATION_TRANSITIONS = "application_transitions"
CONF_CLOSE_TARGET_X = "close_target_x"
CONF_CLOSE_TARGET_Y = "close_target_y"
CONF_INTERNAL_COMPOSITOR_ID = "internal_compositor_id"
CONF_MAX_ENTRIES = "max_entries"
CONF_PRELOAD = "preload"
CONF_QUALITY = "quality"
CONF_SETTLE_DURATION = "settle_duration"
CONF_SNAPSHOT_COMPOSITOR = "snapshot_compositor"
CONF_START_SIZE = "start_size"

COMPRESSION_NONE = "none"
COMPRESSION_JPEG = "jpeg"

APPLICATION_TRANSITIONS_SCHEMA = cv.Schema(
    {
        cv.Optional(
            CONF_OPEN_DURATION, default="500ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_CLOSE_DURATION, default="500ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_START_SIZE, default="1%"): cv.percentage,
        cv.Optional(CONF_CLOSE_TARGET_X, default="50%"): cv.percentage,
        cv.Optional(CONF_CLOSE_TARGET_Y, default="75%"): cv.percentage,
    }
)


def _validate_compression(value):
    value = cv.one_of(COMPRESSION_NONE, COMPRESSION_JPEG, lower=True)(value)
    if value == COMPRESSION_JPEG:
        cv.requires_component("esp32_jpeg")(value)
    return value


SNAPSHOT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LvglSnapshotStore),
        cv.GenerateID(CONF_INTERNAL_COMPOSITOR_ID): cv.declare_id(
            LvglSnapshotCompositor
        ),
        cv.Optional(CONF_COMPRESSION, default=COMPRESSION_NONE): _validate_compression,
        cv.Optional(CONF_QUALITY, default=90): cv.int_range(min=1, max=100),
        cv.Optional(CONF_MAX_ENTRIES, default=16): cv.int_range(min=1, max=64),
        cv.Optional(CONF_DECODED_SLOTS, default=3): cv.int_range(min=1, max=8),
        cv.Optional(CONF_PRELOAD, default=False): cv.boolean,
        cv.Optional(
            CONF_SETTLE_DURATION, default="220ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_APPLICATION_TRANSITIONS): APPLICATION_TRANSITIONS_SCHEMA,
        cv.Optional(CONF_PAGES, default=[]): cv.ensure_list(cv.use_id(lv_page_t)),
    }
).extend(cv.COMPONENT_SCHEMA)


def _registered_page_ids(config, navigation_config):
    page_ids = list(config[CONF_PAGES])
    if navigation_config is not None:
        page_ids.extend(navigation_config[CONF_HOME][CONF_PAGES])
        page_ids.extend(
            application[CONF_PAGE]
            for application in navigation_config[CONF_APPLICATIONS]
        )
    return list(dict.fromkeys(page_ids))


async def snapshot_to_code(lv_component, config, navigation_config):
    snapshot_config = config.get(CONF_SNAPSHOT_COMPOSITOR)
    if snapshot_config is None:
        return

    page_ids = _registered_page_ids(snapshot_config, navigation_config)
    if len(page_ids) > snapshot_config[CONF_MAX_ENTRIES]:
        raise cv.Invalid(
            f"snapshot_compositor registers {len(page_ids)} pages, but max_entries "
            f"is {snapshot_config[CONF_MAX_ENTRIES]}"
        )

    store = cg.new_Pvariable(snapshot_config[CONF_ID], lv_component)
    await cg.register_component(store, snapshot_config)
    compression = (
        "SnapshotCompression::JPEG"
        if snapshot_config[CONF_COMPRESSION] == COMPRESSION_JPEG
        else "SnapshotCompression::NONE"
    )
    cg.add(store.set_compression(cg.RawExpression(compression)))
    cg.add(store.set_quality(snapshot_config[CONF_QUALITY]))
    cg.add(store.set_max_entries(snapshot_config[CONF_MAX_ENTRIES]))
    cg.add(store.set_decoded_slots(snapshot_config[CONF_DECODED_SLOTS]))
    cg.add(store.set_preload(snapshot_config[CONF_PRELOAD]))

    for page_id in page_ids:
        page = await cg.get_variable(page_id)
        cg.add(store.register_page(page))

    if navigation_config is not None:
        navigation = await cg.get_variable(navigation_config[CONF_ID])
        compositor = cg.new_Pvariable(
            snapshot_config[CONF_INTERNAL_COMPOSITOR_ID],
            lv_component,
            store,
        )
        cg.add(
            compositor.set_settle_duration(
                snapshot_config[CONF_SETTLE_DURATION].total_milliseconds
            )
        )
        for page_id in navigation_config[CONF_HOME][CONF_PAGES]:
            page = await cg.get_variable(page_id)
            cg.add(compositor.add_home_page(page))
        if transitions := snapshot_config.get(CONF_APPLICATION_TRANSITIONS):
            cg.add(compositor.set_application_transitions_enabled(True))
            cg.add(
                compositor.set_application_open_duration(
                    transitions[CONF_OPEN_DURATION].total_milliseconds
                )
            )
            cg.add(
                compositor.set_application_close_duration(
                    transitions[CONF_CLOSE_DURATION].total_milliseconds
                )
            )
            cg.add(compositor.set_application_start_ratio(transitions[CONF_START_SIZE]))
            cg.add(
                compositor.set_application_close_target(
                    transitions[CONF_CLOSE_TARGET_X],
                    transitions[CONF_CLOSE_TARGET_Y],
                )
            )
        cg.add(navigation.set_snapshot_compositor(compositor))


SNAPSHOT_PAGE_ACTION_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_ID): cv.use_id(LvglSnapshotStore),
        cv.Required(CONF_PAGE): cv.use_id(lv_page_t),
    }
)

SNAPSHOT_STORE_ACTION_SCHEMA = cv.maybe_simple_value(
    cv.Schema({cv.Required(CONF_ID): cv.use_id(LvglSnapshotStore)}),
    key=CONF_ID,
)


@automation.register_action(
    "lvgl.snapshot.capture",
    SnapshotCaptureAction,
    SNAPSHOT_PAGE_ACTION_SCHEMA,
    synchronous=True,
)
async def snapshot_capture_to_code(config, action_id, template_arg, args):
    store = await cg.get_variable(config[CONF_ID])
    page = await cg.get_variable(config[CONF_PAGE])
    return cg.new_Pvariable(action_id, template_arg, store, page)


@automation.register_action(
    "lvgl.snapshot.capture_all",
    SnapshotCaptureAllAction,
    SNAPSHOT_STORE_ACTION_SCHEMA,
    synchronous=True,
)
async def snapshot_capture_all_to_code(config, action_id, template_arg, args):
    store = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, store)


@automation.register_action(
    "lvgl.snapshot.invalidate",
    SnapshotInvalidateAction,
    SNAPSHOT_PAGE_ACTION_SCHEMA,
    synchronous=True,
)
async def snapshot_invalidate_to_code(config, action_id, template_arg, args):
    store = await cg.get_variable(config[CONF_ID])
    page = await cg.get_variable(config[CONF_PAGE])
    return cg.new_Pvariable(action_id, template_arg, store, page)


@automation.register_action(
    "lvgl.snapshot.clear",
    SnapshotClearAction,
    SNAPSHOT_STORE_ACTION_SCHEMA,
    synchronous=True,
)
async def snapshot_clear_to_code(config, action_id, template_arg, args):
    store = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, store)
