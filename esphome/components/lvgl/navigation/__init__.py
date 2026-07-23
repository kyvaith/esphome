from esphome import automation, codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PAGES

from ..lvcode import lv_add
from ..types import (
    LvglApplication,
    LvglNavigation,
    NavigationCloseAction,
    NavigationHomeAction,
    NavigationIsOpenCondition,
    NavigationOpenAction,
    lv_page_t,
)

CONF_APPLICATIONS = "applications"
CONF_AXIS_BIAS = "axis_bias"
CONF_CLOSE_COMMIT_THRESHOLD = "close_commit_threshold"
CONF_CLOSE_EDGE_SIZE = "close_edge_size"
CONF_CLOSE_GESTURE = "close_gesture"
CONF_HOME = "home"
CONF_HOME_COMMIT_THRESHOLD = "home_commit_threshold"
CONF_NAVIGATION = "navigation"
CONF_PAGE = "page"
CONF_SWIPE_START_DISTANCE = "swipe_start_distance"


APPLICATION_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(LvglApplication),
        cv.Required(CONF_PAGE): cv.use_id(lv_page_t),
        cv.Optional(CONF_CLOSE_GESTURE, default=True): cv.boolean,
    }
)

HOME_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_PAGES): cv.All(
            cv.ensure_list(cv.use_id(lv_page_t)),
            cv.Length(min=1),
        ),
    }
)


def _validate_navigation(config):
    home_pages = config[CONF_HOME][CONF_PAGES]
    application_pages = [
        application[CONF_PAGE] for application in config[CONF_APPLICATIONS]
    ]
    if len(set(home_pages)) != len(home_pages):
        raise cv.Invalid("Home page IDs must be unique")
    if len(set(application_pages)) != len(application_pages):
        raise cv.Invalid("Application page IDs must be unique")
    if set(home_pages) & set(application_pages):
        raise cv.Invalid("A page cannot be both a home page and an application")
    return config


NAVIGATION_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(LvglNavigation),
            cv.Required(CONF_HOME): HOME_SCHEMA,
            cv.Optional(CONF_APPLICATIONS, default=[]): cv.ensure_list(
                APPLICATION_SCHEMA
            ),
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

    for page_id in navigation_config[CONF_HOME][CONF_PAGES]:
        page = await cg.get_variable(page_id)
        lv_add(navigation.add_home_page(page))

    for application_config in navigation_config[CONF_APPLICATIONS]:
        page = await cg.get_variable(application_config[CONF_PAGE])
        application = cg.new_Pvariable(application_config[CONF_ID])
        lv_add(application.set_page(page))
        lv_add(
            application.set_close_gesture_enabled(
                application_config[CONF_CLOSE_GESTURE]
            )
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
