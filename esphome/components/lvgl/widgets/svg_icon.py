"""Bounded monochrome SVG paths drawn by LVGL, without an image asset or worker."""

import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET

from esphome import codegen as cg, config_validation as cv
from esphome.const import CONF_FILE, CONF_HEIGHT, CONF_RAW_DATA_ID, CONF_WIDTH
from esphome.core import CORE

from ..defines import CONF_MAIN, DOMAIN, add_lv_use
from ..lvcode import lv_add
from ..types import lv_obj_t, lvgl_ns
from . import Widget, WidgetType

CONF_ICON_ID = "icon_id"
SvgIcon = lvgl_ns.class_("SvgIcon")
MAX_SOURCE_BYTES = 65536
MAX_COMMANDS = 256
NUMBER = r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?"
TOKEN = re.compile(rf"[MLCQZ]|{NUMBER}")
PARAMETERS = {"M": 2, "L": 2, "Q": 4, "C": 6, "Z": 0}


def _numbers(value: str, count: int) -> tuple[float, ...]:
    parts = re.split(r"[\s,]+", value.strip())
    try:
        values = tuple(float(part) for part in parts)
    except ValueError as error:
        raise cv.Invalid("SVG requires finite numeric coordinates") from error
    if len(values) != count or not all(math.isfinite(v) and abs(v) <= 65536 for v in values):
        raise cv.Invalid("SVG has invalid or excessive coordinates")
    return values


def parse_path(value: str) -> list[tuple[str, tuple[float, ...]]]:
    """Accept closed absolute M/L/Q/C/Z contours; do not silently ignore SVG features."""
    if TOKEN.sub("", value).strip(" \t\r\n,"):
        raise cv.Invalid("SVG icon paths support only absolute M, L, Q, C and Z commands")
    tokens = TOKEN.findall(value)
    result = []
    index = 0
    command = None
    opened = False
    while index < len(tokens):
        if tokens[index] in PARAMETERS:
            command = tokens[index]
            index += 1
        if command is None:
            raise cv.Invalid("SVG path must start with M")
        if command == "Z":
            if not opened:
                raise cv.Invalid("SVG path closes without a contour")
            result.append((command, ()))
            opened = False
            command = None
            continue
        if command != "M" and not opened:
            raise cv.Invalid("SVG contour must start with M")
        if command == "M" and opened:
            raise cv.Invalid("SVG contours must be closed with Z")
        count = PARAMETERS[command]
        if index + count > len(tokens):
            raise cv.Invalid("SVG path command has too few coordinates")
        values = _numbers(" ".join(tokens[index : index + count]), count)
        result.append((command, values))
        opened = True
        index += count
        if command == "M":
            command = "L"
        if len(result) > MAX_COMMANDS:
            raise cv.Invalid("SVG icon has more than 256 path commands")
    if opened or not result:
        raise cv.Invalid("SVG icon paths must be nonempty and closed with Z")
    return result


def read_svg(path: str) -> tuple[tuple[float, ...], list]:
    source = Path(path)
    try:
        if source.stat().st_size > MAX_SOURCE_BYTES:
            raise cv.Invalid("SVG icon source exceeds 64 KiB")
        text = source.read_text(encoding="utf-8")
        if "<!DOCTYPE" in text.upper() or "<!ENTITY" in text.upper():
            raise cv.Invalid("SVG icon must not contain XML entities or a document type")
        root = ET.fromstring(text)
    except (OSError, UnicodeError, ET.ParseError) as error:
        raise cv.Invalid(f"Cannot read SVG icon: {error}") from error
    prefix = "{http://www.w3.org/2000/svg}"
    if root.tag not in {"svg", f"{prefix}svg"}:
        raise cv.Invalid("SVG icon needs an svg root")
    if set(root.attrib) - {"viewBox", "width", "height", "fill"}:
        raise cv.Invalid("SVG icon root supports only viewBox, width, height and fill")
    box = _numbers(root.get("viewBox", ""), 4)
    if box[2] < 0.001 or box[3] < 0.001:
        raise cv.Invalid("SVG viewBox dimensions must be at least 0.001")
    fill = root.get("fill", "currentColor")
    if not re.fullmatch(r"(?:#[0-9a-fA-F]{3}|#[0-9a-fA-F]{6}|black|white|currentColor)", fill):
        raise cv.Invalid("SVG icon requires one solid fill")
    commands = []
    for child in root:
        tag = child.tag.removeprefix(prefix)
        if tag in {"title", "desc"} and not child.attrib and not len(child):
            continue
        if tag != "path" or set(child.attrib) - {"d", "fill"} or len(child):
            raise cv.Invalid("SVG icons support only filled paths, without transforms, strokes or images")
        if child.get("fill", fill) != fill:
            raise cv.Invalid("All SVG icon paths must use the same solid fill")
        commands.extend(parse_path(child.get("d", "")))
        if len(commands) > MAX_COMMANDS:
            raise cv.Invalid("SVG icon has more than 256 path commands")
    if not commands:
        raise cv.Invalid("SVG icon has no paths")
    return box, commands


def validate_svg_file(value):
    path = CORE.relative_config_path(cv.string(value))
    read_svg(path)
    return path


SVG_ICON_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_FILE): validate_svg_file,
        cv.Required(CONF_WIDTH): cv.int_range(min=1, max=256),
        cv.Required(CONF_HEIGHT): cv.int_range(min=1, max=256),
        cv.GenerateID(CONF_ICON_ID): cv.declare_id(SvgIcon),
        cv.GenerateID(CONF_RAW_DATA_ID): cv.declare_id(cg.uint8),
    }
)


def _cpp_float(value: float) -> str:
    text = format(value, ".9g")
    return f"{text}{'' if '.' in text or 'e' in text else '.0'}f"


class SvgIconType(WidgetType):
    def __init__(self):
        super().__init__("svg_icon", lv_obj_t, (CONF_MAIN,), SVG_ICON_SCHEMA, modify_schema={}, lv_name="obj")

    def get_uses(self):
        return ("VECTOR_GRAPHIC", "THORVG_INTERNAL")

    async def on_create(self, var, config):
        lv_add(cg.RawStatement(
            f"lv_obj_set_style_bg_opa({var}, LV_OPA_TRANSP, LV_PART_MAIN);\n"
            f"lv_obj_set_style_border_width({var}, 0, LV_PART_MAIN);\n"
            f"lv_obj_set_style_pad_all({var}, 0, LV_PART_MAIN);\n"
            f"lv_obj_remove_flag({var}, LV_OBJ_FLAG_SCROLLABLE);"
        ))

    async def update_to_code(self, w: Widget, config):
        # Geometry is immutable. Ordinary styles and size are updated by the widget base.
        pass

    async def to_code(self, w: Widget, config):
        add_lv_use("VECTOR_GRAPHIC", "THORVG_INTERNAL")
        data = CORE.data.setdefault(DOMAIN, {})
        if not data.get("svg_icon_header"):
            cg.add_global(cg.RawStatement('#include "esphome/components/lvgl/svg_icon.h"'))
            data["svg_icon_header"] = True
        box, commands = read_svg(config[CONF_FILE])
        raw_id = config[CONF_RAW_DATA_ID]
        rows = []
        for op, values in commands:
            padded = values + (0.0,) * (6 - len(values))
            rows.append("{'" + op + "', {" + ", ".join(_cpp_float(v) for v in padded) + "}}")
        cg.add_global(cg.RawStatement(
            f"static const esphome::lvgl::SvgIconCommand {raw_id}[] PROGMEM = {{\n  "
            + ",\n  ".join(rows) + "\n};"
        ))
        icon = cg.new_Pvariable(config[CONF_ICON_ID])
        lv_add(icon.setup(w.obj, cg.RawExpression(str(raw_id)), len(commands), *box))


svg_icon_spec = SvgIconType()
