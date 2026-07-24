from esphome import automation, codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_CLOSE_DURATION,
    CONF_COMPRESSION,
    CONF_ID,
    CONF_OPEN_DURATION,
    CONF_PAGES,
)

from ..defines import CONF_WIDGETS
from ..lvcode import lv_add
from ..navigation import (
    CONF_APPLICATIONS,
    CONF_AXIS_BIAS,
    CONF_HOME,
    CONF_PAGE,
    CONF_SWIPE_START_DISTANCE,
)
from ..types import (
    LvglApplication,
    LvglDirectSnapshotCompositor,
    LvglScrollSnapshotController,
    LvglSnapshotCompositor,
    LvglSnapshotStore,
    ScrollSnapshotPrepareAction,
    ScrollSnapshotRefreshAction,
    ScrollSnapshotReleaseAction,
    SnapshotCaptureAction,
    SnapshotCaptureAllAction,
    SnapshotClearAction,
    SnapshotInvalidateAction,
    lv_page_t,
    lv_pseudo_button_t,
)
from ..widgets import get_widgets

CONF_APPLICATION = "application"
CONF_BACKEND = "backend"
CONF_DECODED_SLOTS = "decoded_slots"
CONF_APPLICATION_TRANSITIONS = "application_transitions"
CONF_CLOSE_TARGET_X = "close_target_x"
CONF_CLOSE_TARGET_Y = "close_target_y"
CONF_HOME_VIEWS = "home_views"
CONF_INTERNAL_COMPOSITOR_ID = "internal_compositor_id"
CONF_MAX_ENTRIES = "max_entries"
CONF_PRELOAD = "preload"
CONF_QUALITY = "quality"
CONF_SETTLE_DURATION = "settle_duration"
CONF_SNAPSHOT_COMPOSITOR = "snapshot_compositor"
CONF_SCROLL_REGIONS = "scroll_regions"
CONF_START_SIZE = "start_size"
CONF_WIDGET = "widget"
CONF_MAX_CONTENT_SIZE = "max_content_size"
CONF_OVERSCROLL = "overscroll"
CONF_MOMENTUM_DURATION = "momentum_duration"
CONF_BOUNCE_DURATION = "bounce_duration"
CONF_MAX_INERTIA_DURATION = "max_inertia_duration"

COMPRESSION_NONE = "none"
COMPRESSION_JPEG = "jpeg"
BACKEND_LVGL = "lvgl"
BACKEND_DIRECT = "direct"

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

SCROLL_REGION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LvglScrollSnapshotController),
        cv.Required(CONF_APPLICATION): cv.use_id(LvglApplication),
        cv.Required(CONF_WIDGET): cv.use_id(lv_pseudo_button_t),
        cv.Optional(CONF_BACKEND, default=BACKEND_LVGL): cv.one_of(
            BACKEND_LVGL, BACKEND_DIRECT, lower=True
        ),
        cv.Optional(CONF_PRELOAD, default=True): cv.boolean,
        cv.Optional(CONF_MAX_CONTENT_SIZE, default="8MB"): cv.All(
            cv.validate_bytes,
            cv.int_range(min=65536, max=64 * 1024 * 1024),
        ),
        cv.Optional(CONF_OVERSCROLL, default="15%"): cv.percentage,
        cv.Optional(
            CONF_MOMENTUM_DURATION, default="560ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_BOUNCE_DURATION, default="320ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_MAX_INERTIA_DURATION, default="900ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_SWIPE_START_DISTANCE, default=10): cv.int_range(
            min=1, max=1000
        ),
        cv.Optional(CONF_AXIS_BIAS, default=6): cv.int_range(min=0, max=1000),
    }
)


def _validate_scroll_regions(config):
    applications = [region[CONF_APPLICATION] for region in config[CONF_SCROLL_REGIONS]]
    widgets = [region[CONF_WIDGET] for region in config[CONF_SCROLL_REGIONS]]
    if len(applications) != len(set(applications)):
        raise cv.Invalid("Only one scroll region can be assigned to an application")
    if len(widgets) != len(set(widgets)):
        raise cv.Invalid("A widget can only be assigned to one scroll region")
    direct_regions = [
        region
        for region in config[CONF_SCROLL_REGIONS]
        if region[CONF_BACKEND] == BACKEND_DIRECT
    ]
    if len(direct_regions) > 1:
        raise cv.Invalid(
            "Only one direct scroll region can be configured per LVGL display"
        )
    if direct_regions and config[CONF_BACKEND] != BACKEND_DIRECT:
        raise cv.Invalid(
            "A direct scroll region requires snapshot_compositor.backend: direct"
        )
    return config


def _validate_compression(value):
    value = cv.one_of(COMPRESSION_NONE, COMPRESSION_JPEG, lower=True)(value)
    if value == COMPRESSION_JPEG:
        cv.requires_component("esp32_jpeg")(value)
    return value


SNAPSHOT_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LvglSnapshotStore),
            cv.GenerateID(CONF_INTERNAL_COMPOSITOR_ID): cv.declare_id(
                LvglSnapshotCompositor
            ),
            cv.Optional(CONF_BACKEND, default=BACKEND_LVGL): cv.one_of(
                BACKEND_LVGL, BACKEND_DIRECT, lower=True
            ),
            cv.Optional(
                CONF_COMPRESSION, default=COMPRESSION_NONE
            ): _validate_compression,
            cv.Optional(CONF_QUALITY, default=90): cv.int_range(min=1, max=100),
            cv.Optional(CONF_MAX_ENTRIES, default=16): cv.int_range(min=1, max=64),
            cv.Optional(CONF_DECODED_SLOTS, default=3): cv.int_range(min=1, max=8),
            cv.Optional(CONF_PRELOAD, default=False): cv.boolean,
            cv.Optional(
                CONF_SETTLE_DURATION, default="220ms"
            ): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_APPLICATION_TRANSITIONS): APPLICATION_TRANSITIONS_SCHEMA,
            cv.Optional(CONF_SCROLL_REGIONS, default=[]): cv.ensure_list(
                SCROLL_REGION_SCHEMA
            ),
            cv.Optional(CONF_HOME_VIEWS, default=[]): cv.ensure_list(
                cv.use_id(lv_pseudo_button_t)
            ),
            cv.Optional(CONF_PAGES, default=[]): cv.ensure_list(cv.use_id(lv_page_t)),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_scroll_regions,
)


def _registered_page_ids(config, navigation_config, include_home_pages):
    page_ids = list(config[CONF_PAGES])
    if navigation_config is not None:
        if include_home_pages:
            page_ids.extend(navigation_config[CONF_HOME][CONF_PAGES])
        page_ids.extend(
            application[CONF_PAGE]
            for application in navigation_config[CONF_APPLICATIONS]
            if CONF_WIDGET not in application
        )
    return list(dict.fromkeys(page_ids))


def _registered_home_widget_ids(navigation_config):
    if navigation_config is None:
        return []
    return list(dict.fromkeys(navigation_config[CONF_HOME][CONF_WIDGETS]))


def _registered_application_widget_ids(navigation_config):
    if navigation_config is None:
        return []
    return list(
        dict.fromkeys(
            application[CONF_WIDGET]
            for application in navigation_config[CONF_APPLICATIONS]
            if CONF_WIDGET in application
        )
    )


async def snapshot_to_code(lv_component, config, navigation_config):
    snapshot_config = config.get(CONF_SNAPSHOT_COMPOSITOR)
    if snapshot_config is None:
        return

    configured_home_view_ids = snapshot_config[CONF_HOME_VIEWS]
    navigation_home_widget_ids = _registered_home_widget_ids(navigation_config)
    if configured_home_view_ids and navigation_home_widget_ids:
        raise cv.Invalid(
            "snapshot_compositor.home_views is only valid with page-based home navigation"
        )

    home_view_ids = configured_home_view_ids or navigation_home_widget_ids
    navigation_home_page_ids = (
        navigation_config[CONF_HOME][CONF_PAGES]
        if navigation_config is not None
        else []
    )
    if configured_home_view_ids and len(configured_home_view_ids) != len(
        navigation_home_page_ids
    ):
        raise cv.Invalid(
            "snapshot_compositor.home_views must contain one widget for every "
            "page in navigation.home.pages"
        )

    application_widget_ids = _registered_application_widget_ids(navigation_config)
    direct_backend = snapshot_config[CONF_BACKEND] == BACKEND_DIRECT
    if direct_backend and navigation_config is not None and not home_view_ids:
        raise cv.Invalid(
            "The direct snapshot backend requires home widget navigation or "
            "snapshot_compositor.home_views"
        )

    page_ids = _registered_page_ids(
        snapshot_config,
        navigation_config,
        include_home_pages=not (direct_backend and configured_home_view_ids),
    )
    registered_count = (
        len(page_ids)
        + len(application_widget_ids)
        + (0 if direct_backend else len(home_view_ids))
    )
    if registered_count > snapshot_config[CONF_MAX_ENTRIES]:
        raise cv.Invalid(
            f"snapshot_compositor registers {registered_count} views, but max_entries "
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
    home_views = await get_widgets(
        [{CONF_ID: widget_id} for widget_id in home_view_ids]
    )
    application_widgets = await get_widgets(
        [{CONF_ID: widget_id} for widget_id in application_widget_ids]
    )
    if not direct_backend:
        for widget in home_views:
            lv_add(store.register_object(widget.obj))
    for widget in application_widgets:
        lv_add(store.register_object(widget.obj))

    if navigation_config is not None:
        navigation = await cg.get_variable(navigation_config[CONF_ID])
        compositor_type = (
            LvglDirectSnapshotCompositor if direct_backend else LvglSnapshotCompositor
        )
        compositor = cg.Pvariable(
            snapshot_config[CONF_INTERNAL_COMPOSITOR_ID],
            compositor_type.new(lv_component, store),
            type_=LvglSnapshotCompositor,
        )
        cg.add(
            compositor.set_settle_duration(
                snapshot_config[CONF_SETTLE_DURATION].total_milliseconds
            )
        )
        home_config = navigation_config[CONF_HOME]
        if home_config[CONF_PAGES]:
            if configured_home_view_ids:
                for widget in home_views:
                    lv_add(compositor.add_home_view(widget.obj))
            else:
                for page_id in home_config[CONF_PAGES]:
                    page = await cg.get_variable(page_id)
                    cg.add(compositor.add_home_page(page))
        else:
            for widget in home_views:
                lv_add(compositor.add_home_view(widget.obj))
        if direct_backend and snapshot_config[CONF_PRELOAD]:
            lv_add(compositor.prepare_home(0))
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

        for region_config in snapshot_config[CONF_SCROLL_REGIONS]:
            application = await cg.get_variable(region_config[CONF_APPLICATION])
            widget = (
                await get_widgets(
                    [{CONF_ID: region_config[CONF_WIDGET]}],
                )
            )[0]
            controller = cg.new_Pvariable(
                region_config[CONF_ID],
                lv_component,
                application.get_page(),
                widget.obj,
            )
            backend = (
                "ScrollSnapshotBackend::DIRECT"
                if region_config[CONF_BACKEND] == BACKEND_DIRECT
                else "ScrollSnapshotBackend::LVGL"
            )
            cg.add(controller.set_backend(cg.RawExpression(backend)))
            cg.add(controller.set_preload(region_config[CONF_PRELOAD]))
            cg.add(
                controller.set_max_content_bytes(region_config[CONF_MAX_CONTENT_SIZE])
            )
            cg.add(controller.set_overscroll_ratio(region_config[CONF_OVERSCROLL]))
            cg.add(
                controller.set_momentum_duration(
                    region_config[CONF_MOMENTUM_DURATION].total_milliseconds
                )
            )
            cg.add(
                controller.set_bounce_duration(
                    region_config[CONF_BOUNCE_DURATION].total_milliseconds
                )
            )
            cg.add(
                controller.set_max_inertia_duration(
                    region_config[CONF_MAX_INERTIA_DURATION].total_milliseconds
                )
            )
            cg.add(
                controller.set_start_distance(region_config[CONF_SWIPE_START_DISTANCE])
            )
            cg.add(controller.set_axis_bias(region_config[CONF_AXIS_BIAS]))
            cg.add(application.set_scroll_snapshot(controller))
            cg.add(controller.setup())


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


SCROLL_SNAPSHOT_ACTION_SCHEMA = cv.maybe_simple_value(
    cv.Schema({cv.Required(CONF_ID): cv.use_id(LvglScrollSnapshotController)}),
    key=CONF_ID,
)


@automation.register_action(
    "lvgl.snapshot_scroll.prepare",
    ScrollSnapshotPrepareAction,
    SCROLL_SNAPSHOT_ACTION_SCHEMA,
    synchronous=True,
)
async def scroll_snapshot_prepare_to_code(config, action_id, template_arg, args):
    controller = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, controller)


@automation.register_action(
    "lvgl.snapshot_scroll.refresh",
    ScrollSnapshotRefreshAction,
    SCROLL_SNAPSHOT_ACTION_SCHEMA,
    synchronous=True,
)
async def scroll_snapshot_refresh_to_code(config, action_id, template_arg, args):
    controller = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, controller)


@automation.register_action(
    "lvgl.snapshot_scroll.release",
    ScrollSnapshotReleaseAction,
    SCROLL_SNAPSHOT_ACTION_SCHEMA,
    synchronous=True,
)
async def scroll_snapshot_release_to_code(config, action_id, template_arg, args):
    controller = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, controller)
