"""
LVGL v9.4 Menu Widget Implementation

The menu widget provides hierarchical navigation with:
- Multiple pages/screens
- Header with title and back button
- Main content area
- Sidebar support
- Breadcrumb navigation
"""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_MODE

from ..defines import (
    CONF_BODY,
    CONF_HEADER,
    CONF_MAIN,
    CONF_PAGE,
    CONF_SIDEBAR,
    CONF_TITLE,
    add_lv_use,
    literal,
)
from ..lv_validation import lv_bool, lv_text
from ..lvcode import lv, lv_assign, lv_expr, lv_Pvariable
from ..schemas import container_schema
from ..types import LvType, lv_obj_t
from . import Widget, WidgetType, add_widgets, set_obj_properties
from .obj import obj_spec

CONF_MENU = "menu"
CONF_PAGES = "pages"
CONF_ROOT_BACK_BUTTON = "root_back_button"
CONF_SIDEBAR_PAGE = "sidebar_page"

lv_menu_t = LvType("lv_menu_t")
lv_menu_page_t = LvType("lv_obj_t")

# Header display modes for lv_menu_set_mode_header()
MENU_HEADER_MODES = {
    "TOP_FIXED": "LV_MENU_HEADER_TOP_FIXED",
    "TOP_UNFIXED": "LV_MENU_HEADER_TOP_UNFIXED",
    "BOTTOM_FIXED": "LV_MENU_HEADER_BOTTOM_FIXED",
}

# Menu page schema
MENU_PAGE_SCHEMA = container_schema(
    obj_spec,
    {
        cv.GenerateID(): cv.declare_id(lv_menu_page_t),
        cv.Optional(CONF_TITLE): lv_text,
    },
)

# Main menu schema
MENU_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_PAGES): cv.ensure_list(MENU_PAGE_SCHEMA),
        cv.Optional(CONF_ROOT_BACK_BUTTON, default=False): lv_bool,
        cv.Optional(CONF_MODE, default="TOP_FIXED"): cv.enum(
            MENU_HEADER_MODES, upper=True
        ),
        cv.Optional(CONF_SIDEBAR_PAGE): cv.use_id(lv_menu_page_t),
    }
)


class MenuType(WidgetType):
    def __init__(self):
        super().__init__(
            CONF_MENU,
            lv_menu_t,
            (CONF_MAIN, CONF_HEADER, CONF_SIDEBAR),
            MENU_SCHEMA,
            modify_schema={},
        )

    async def to_code(self, w: Widget, config):
        """Generate C++ code for menu widget configuration"""
        add_lv_use(CONF_MENU)

        # Set header display mode
        header_mode = MENU_HEADER_MODES[config[CONF_MODE]]
        lv.menu_set_mode_header(w.obj, literal(header_mode))

        # Set root back button mode
        if config.get(CONF_ROOT_BACK_BUTTON):
            lv.menu_set_mode_root_back_button(w.obj, literal("LV_MENU_ROOT_BACK_BUTTON_ENABLED"))
        else:
            lv.menu_set_mode_root_back_button(w.obj, literal("LV_MENU_ROOT_BACK_BUTTON_DISABLED"))

        # Create menu pages
        first_page = None
        if pages := config.get(CONF_PAGES):
            for page_conf in pages:
                page_obj = await self._create_page(w, page_conf)
                if first_page is None:
                    first_page = page_obj

        # Set sidebar page if specified
        if sidebar_page_id := config.get(CONF_SIDEBAR_PAGE):
            sidebar_page = await cg.get_variable(sidebar_page_id)
            lv.menu_set_sidebar_page(w.obj, sidebar_page)

        # Set first page as the main page so the menu is not empty
        if first_page is not None:
            lv.menu_set_page(w.obj, first_page)

    async def _create_page(self, w: Widget, page_conf):
        """Create a menu page with optional title and content"""
        page_id = page_conf[CONF_ID]

        # Create the menu page variable (lv_obj_t *)
        page_obj = lv_Pvariable(lv_obj_t, page_id)
        if CONF_TITLE in page_conf:
            title = await lv_text.process(page_conf[CONF_TITLE])
            lv_assign(page_obj, lv_expr.menu_page_create(w.obj, title))
        else:
            lv_assign(page_obj, lv_expr.menu_page_create(w.obj, literal("NULL")))

        # Create widget wrapper for the page
        page_widget = Widget.create(page_id, page_obj, obj_spec, page_conf)

        # Set page properties and add child widgets
        await set_obj_properties(page_widget, page_conf)
        await add_widgets(page_widget, page_conf)

        return page_obj

    def get_uses(self):
        """Menu widget uses button and label for navigation"""
        return ("btn", "label")


menu_spec = MenuType()
