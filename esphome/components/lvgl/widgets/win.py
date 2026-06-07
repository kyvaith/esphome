"""
LVGL v9.5 Window Widget Implementation

The window widget provides a header bar with title/buttons and a content area.
In LVGL v9, lv_win_create(parent) creates a flex-column container with:
- A header row (for title label and buttons)
- A content area (occupies remaining space)
"""

import esphome.config_validation as cv
from esphome.const import CONF_ID

from ..defines import (
    CONF_BODY,
    CONF_HEADER,
    CONF_MAIN,
    CONF_SRC,
    CONF_TITLE,
    add_lv_use,
    literal,
)
from ..lv_validation import lv_image, lv_text
from ..lvcode import lv, lv_expr
from ..types import LvType
from . import Widget, WidgetType

CONF_WIN = "win"
CONF_HEADER_BUTTONS = "header_buttons"

lv_win_t = LvType("lv_win_t")

# Header button schema
HEADER_BUTTON_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_SRC): lv_image,
    }
)

# Window schema - simple widget, no child widgets support
# (children would need to go to lv_win_get_content(), not the window itself)
WIN_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_TITLE): lv_text,
        cv.Optional(CONF_HEADER_BUTTONS): cv.ensure_list(HEADER_BUTTON_SCHEMA),
    }
)


class WindowType(WidgetType):
    def __init__(self):
        super().__init__(
            CONF_WIN,
            lv_win_t,
            (CONF_MAIN, CONF_HEADER, CONF_BODY),
            WIN_SCHEMA,
            modify_schema={},
        )

    async def to_code(self, w: Widget, config):
        add_lv_use(CONF_WIN)

        # Add title to window header
        title = await lv_text.process(config[CONF_TITLE])
        lv.win_add_title(w.obj, title)

        # Add header buttons if specified
        if header_buttons := config.get(CONF_HEADER_BUTTONS):
            for button_conf in header_buttons:
                if CONF_SRC in button_conf:
                    icon = await lv_image.process(button_conf[CONF_SRC])
                    lv.win_add_button(w.obj, icon, literal("40"))
                else:
                    lv.win_add_button(w.obj, literal("NULL"), literal("40"))

    def get_uses(self):
        return ("btn", "label", "img")


win_spec = WindowType()
