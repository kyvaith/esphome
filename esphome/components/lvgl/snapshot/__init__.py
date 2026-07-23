from esphome import automation, codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_COMPRESSION, CONF_ID, CONF_PAGES

from ..navigation import CONF_APPLICATIONS, CONF_HOME, CONF_PAGE
from ..types import (
    LvglSnapshotStore,
    SnapshotCaptureAction,
    SnapshotCaptureAllAction,
    SnapshotClearAction,
    SnapshotInvalidateAction,
    lv_page_t,
)

CONF_DECODED_SLOTS = "decoded_slots"
CONF_MAX_ENTRIES = "max_entries"
CONF_PRELOAD = "preload"
CONF_QUALITY = "quality"
CONF_SNAPSHOT_COMPOSITOR = "snapshot_compositor"

COMPRESSION_NONE = "none"
COMPRESSION_JPEG = "jpeg"


def _validate_compression(value):
    value = cv.one_of(COMPRESSION_NONE, COMPRESSION_JPEG, lower=True)(value)
    if value == COMPRESSION_JPEG:
        cv.requires_component("esp32_jpeg")(value)
    return value


SNAPSHOT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LvglSnapshotStore),
        cv.Optional(CONF_COMPRESSION, default=COMPRESSION_NONE): _validate_compression,
        cv.Optional(CONF_QUALITY, default=90): cv.int_range(min=1, max=100),
        cv.Optional(CONF_MAX_ENTRIES, default=16): cv.int_range(min=1, max=64),
        cv.Optional(CONF_DECODED_SLOTS, default=3): cv.int_range(min=1, max=8),
        cv.Optional(CONF_PRELOAD, default=False): cv.boolean,
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
