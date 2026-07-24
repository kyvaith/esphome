from esphome import automation, codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_ON_OPEN, CONF_PAGES

from ..defines import CONF_INDICATORS, CONF_WIDGET, CONF_WIDGETS
from ..lvcode import lv_add
from ..types import (
    LvglApplication,
    LvglNavigation,
    NavigationCloseAction,
    NavigationHomeAction,
    NavigationIsOpenCondition,
    NavigationOpenAction,
    NavigationRefreshAction,
    lv_page_t,
    lv_pseudo_button_t,
)
from ..widgets import get_widgets

CONF_APPLICATIONS = "applications"
CONF_AXIS_BIAS = "axis_bias"
CONF_BLOCKERS = "blockers"
CONF_CLOSE_COMMIT_THRESHOLD = "close_commit_threshold"
CONF_CLOSE_EDGE_SIZE = "close_edge_size"
CONF_CLOSE_GESTURE = "close_gesture"
CONF_HOME = "home"
CONF_HOME_COMMIT_THRESHOLD = "home_commit_threshold"
CONF_NAVIGATION = "navigation"
CONF_ON_CLOSE = "on_close"
CONF_ON_CLOSE_CANCELLED = "on_close_cancelled"
CONF_ON_CLOSED = "on_closed"
CONF_ON_OPENED = "on_opened"
CONF_ON_HOME_CHANGED = "on_home_changed"
CONF_ON_PREPARE_CLOSE = "on_prepare_close"
CONF_ON_PREPARE_OPEN = "on_prepare_open"
CONF_PAGE = "page"
CONF_SWIPE_START_DISTANCE = "swipe_start_distance"

NAVIGATION_CALLBACK_AUTOMATIONS = (
    automation.CallbackAutomation(
        CONF_ON_HOME_CHANGED,
        "add_on_home_changed_callback",
        [(cg.uint16, "page")],
    ),
)


APPLICATION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LvglApplication),
        cv.Required(CONF_PAGE): cv.use_id(lv_page_t),
        cv.Optional(CONF_WIDGET): cv.use_id(lv_pseudo_button_t),
        cv.Optional(CONF_CLOSE_GESTURE, default=True): cv.boolean,
        cv.Optional(CONF_ON_PREPARE_OPEN): automation.validate_automation({}),
        cv.Optional(CONF_ON_OPEN): automation.validate_automation({}),
        cv.Optional(CONF_ON_OPENED): automation.validate_automation({}),
        cv.Optional(CONF_ON_PREPARE_CLOSE): automation.validate_automation({}),
        cv.Optional(CONF_ON_CLOSE): automation.validate_automation({}),
        cv.Optional(CONF_ON_CLOSE_CANCELLED): automation.validate_automation({}),
        cv.Optional(CONF_ON_CLOSED): automation.validate_automation({}),
    }
)

APPLICATION_CALLBACK_AUTOMATIONS = (
    automation.CallbackAutomation(CONF_ON_PREPARE_OPEN, "add_on_prepare_open_callback"),
    automation.CallbackAutomation(CONF_ON_OPEN, "add_on_open_callback"),
    automation.CallbackAutomation(CONF_ON_OPENED, "add_on_opened_callback"),
    automation.CallbackAutomation(
        CONF_ON_PREPARE_CLOSE, "add_on_prepare_close_callback"
    ),
    automation.CallbackAutomation(CONF_ON_CLOSE, "add_on_close_callback"),
    automation.CallbackAutomation(
        CONF_ON_CLOSE_CANCELLED, "add_on_close_cancelled_callback"
    ),
    automation.CallbackAutomation(CONF_ON_CLOSED, "add_on_closed_callback"),
)


def _validate_home(config):
    has_pages = bool(config.get(CONF_PAGES))
    has_widgets = bool(config.get(CONF_WIDGETS))
    indicators = config[CONF_INDICATORS]
    if has_pages == has_widgets:
        raise cv.Invalid(
            "Home navigation requires either 'pages' or the 'page' + 'widgets' pair"
        )
    if has_widgets and CONF_PAGE not in config:
        raise cv.Invalid("Home widget navigation requires a host 'page'")
    if has_pages and CONF_PAGE in config:
        raise cv.Invalid("The home host 'page' is only valid with 'widgets'")
    view_count = len(config[CONF_PAGES] or config[CONF_WIDGETS])
    if indicators and len(indicators) != view_count:
        raise cv.Invalid("Home indicators must contain one widget for every home view")
    return config


HOME_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Optional(CONF_PAGES, default=[]): cv.ensure_list(cv.use_id(lv_page_t)),
            cv.Optional(CONF_PAGE): cv.use_id(lv_page_t),
            cv.Optional(CONF_WIDGETS, default=[]): cv.ensure_list(
                cv.use_id(lv_pseudo_button_t)
            ),
            cv.Optional(CONF_INDICATORS, default=[]): cv.ensure_list(
                cv.use_id(lv_pseudo_button_t)
            ),
        }
    ),
    _validate_home,
)


def _validate_navigation(config):
    home_config = config[CONF_HOME]
    home_pages = home_config[CONF_PAGES]
    home_widgets = home_config[CONF_WIDGETS]
    home_host_page = home_config.get(CONF_PAGE)
    applications = config[CONF_APPLICATIONS]
    page_applications = [
        application for application in applications if CONF_WIDGET not in application
    ]
    application_pages = [application[CONF_PAGE] for application in page_applications]
    application_views = [
        application.get(CONF_WIDGET, application[CONF_PAGE])
        for application in applications
    ]
    if len(set(home_pages)) != len(home_pages):
        raise cv.Invalid("Home page IDs must be unique")
    if len(set(home_widgets)) != len(home_widgets):
        raise cv.Invalid("Home widget IDs must be unique")
    if len(set(application_views)) != len(application_views):
        raise cv.Invalid("Application page or widget IDs must be unique")
    if set(home_pages) & set(application_pages):
        raise cv.Invalid("A page cannot be both a home page and an application")
    if home_host_page in application_pages:
        raise cv.Invalid("The home widget host page cannot also be an application page")
    return config


NAVIGATION_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LvglNavigation),
            cv.Required(CONF_HOME): HOME_SCHEMA,
            cv.Optional(CONF_BLOCKERS, default=[]): cv.ensure_list(
                cv.use_id(lv_pseudo_button_t)
            ),
            cv.Optional(CONF_APPLICATIONS, default=[]): cv.ensure_list(
                APPLICATION_SCHEMA
            ),
            cv.Optional(CONF_ON_HOME_CHANGED): automation.validate_automation({}),
            cv.Optional(CONF_SWIPE_START_DISTANCE, default=10): cv.int_range(
                min=1, max=1000
            ),
            cv.Optional(CONF_AXIS_BIAS, default=6): cv.int_range(min=0, max=1000),
            cv.Optional(CONF_HOME_COMMIT_THRESHOLD, default="25%"): cv.percentage,
            cv.Optional(CONF_CLOSE_EDGE_SIZE, default="6.25%"): cv.percentage,
            cv.Optional(CONF_CLOSE_COMMIT_THRESHOLD, default="25%"): cv.percentage,
        }
    ),
    _validate_navigation,
)


async def navigation_to_code(lv_component, config):
    navigation_config = config.get(CONF_NAVIGATION)
    if navigation_config is None:
        return

    navigation = cg.new_Pvariable(navigation_config[CONF_ID], lv_component)
    cg.add(lv_component.set_navigation(navigation))
    cg.add(
        navigation.set_swipe_start_distance(
            navigation_config[CONF_SWIPE_START_DISTANCE]
        )
    )
    cg.add(navigation.set_axis_bias(navigation_config[CONF_AXIS_BIAS]))
    cg.add(
        navigation.set_home_commit_ratio(navigation_config[CONF_HOME_COMMIT_THRESHOLD])
    )
    cg.add(navigation.set_close_edge_ratio(navigation_config[CONF_CLOSE_EDGE_SIZE]))
    cg.add(
        navigation.set_close_commit_ratio(
            navigation_config[CONF_CLOSE_COMMIT_THRESHOLD]
        )
    )

    home_config = navigation_config[CONF_HOME]
    if home_config[CONF_PAGES]:
        for page_id in home_config[CONF_PAGES]:
            page = await cg.get_variable(page_id)
            lv_add(navigation.add_home_page(page))
    else:
        page = await cg.get_variable(home_config[CONF_PAGE])
        cg.add(navigation.set_home_widget_page(page))
        widgets = await get_widgets(
            [{CONF_ID: widget_id} for widget_id in home_config[CONF_WIDGETS]]
        )
        for widget in widgets:
            lv_add(navigation.add_home_widget(widget.obj))

    indicators = await get_widgets(
        [{CONF_ID: widget_id} for widget_id in home_config[CONF_INDICATORS]]
    )
    for indicator in indicators:
        lv_add(navigation.add_home_indicator(indicator.obj))

    blockers = await get_widgets(
        [{CONF_ID: widget_id} for widget_id in navigation_config[CONF_BLOCKERS]]
    )
    for blocker in blockers:
        lv_add(navigation.add_blocker(blocker.obj))

    await automation.build_callback_automations(
        navigation, navigation_config, NAVIGATION_CALLBACK_AUTOMATIONS
    )

    for application_config in navigation_config[CONF_APPLICATIONS]:
        page = await cg.get_variable(application_config[CONF_PAGE])
        application = cg.new_Pvariable(application_config[CONF_ID])
        lv_add(application.set_page(page))
        if widget_id := application_config.get(CONF_WIDGET):
            widget = (await get_widgets([{CONF_ID: widget_id}]))[0]
            lv_add(application.set_widget(widget.obj))
        lv_add(
            application.set_close_gesture_enabled(
                application_config[CONF_CLOSE_GESTURE]
            )
        )
        await automation.build_callback_automations(
            application, application_config, APPLICATION_CALLBACK_AUTOMATIONS
        )
        lv_add(navigation.add_application(application))


@automation.register_action(
    "lvgl.navigation.open",
    NavigationOpenAction,
    cv.maybe_simple_value(
        cv.Schema({cv.Required(CONF_ID): cv.use_id(LvglApplication)}),
        key=CONF_ID,
    ),
    synchronous=True,
)
async def navigation_open_to_code(config, action_id, template_arg, args):
    application = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(action_id, template_arg, application)


@automation.register_action(
    "lvgl.navigation.close",
    NavigationCloseAction,
    cv.maybe_simple_value(
        cv.Schema({cv.Required(CONF_ID): cv.use_id(LvglNavigation)}),
        key=CONF_ID,
    ),
    synchronous=True,
)
async def navigation_close_to_code(config, action_id, template_arg, args):
    action = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(action, config[CONF_ID])
    return action


@automation.register_action(
    "lvgl.navigation.home",
    NavigationHomeAction,
    cv.maybe_simple_value(
        cv.Schema({cv.Required(CONF_ID): cv.use_id(LvglNavigation)}),
        key=CONF_ID,
    ),
    synchronous=True,
)
async def navigation_home_to_code(config, action_id, template_arg, args):
    action = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(action, config[CONF_ID])
    return action


@automation.register_action(
    "lvgl.navigation.refresh",
    NavigationRefreshAction,
    cv.maybe_simple_value(
        cv.Schema({cv.Required(CONF_ID): cv.use_id(LvglNavigation)}),
        key=CONF_ID,
    ),
    synchronous=True,
)
async def navigation_refresh_to_code(config, action_id, template_arg, args):
    action = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(action, config[CONF_ID])
    return action


@automation.register_condition(
    "lvgl.navigation.is_open",
    NavigationIsOpenCondition,
    cv.maybe_simple_value(
        cv.Schema({cv.Required(CONF_ID): cv.use_id(LvglApplication)}),
        key=CONF_ID,
    ),
)
async def navigation_is_open_to_code(config, condition_id, template_arg, args):
    application = await cg.get_variable(config[CONF_ID])
    return cg.new_Pvariable(condition_id, template_arg, application)
