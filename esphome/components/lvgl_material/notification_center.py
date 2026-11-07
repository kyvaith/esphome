"""Optional bounded notification center; all geometry and product policy are supplied."""

from esphome import automation
import esphome.codegen as cg
from esphome.components.lvgl.defines import CONF_LVGL_ID, add_lv_use
from esphome.components.lvgl.lvcode import LvContext, LvglComponent
from esphome.components.lvgl.types import (
    DirectSceneController,
    LvglNavigation,
    lv_obj_t,
)
from esphome.components.lvgl.widgets import get_widgets
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_PAYLOAD

ns = cg.esphome_ns.namespace("lvgl_material")
MaterialNotificationOverlay = ns.class_("MaterialNotificationOverlay", cg.Component)
MaterialNotificationCenter = ns.class_("MaterialNotificationCenter", cg.Component)
SyncAction = ns.class_("MaterialNotificationSyncAction", automation.Action)
request_admission_slot = cg.slot_counter(
    "ESPHOME_LVGL_NAVIGATION_ADMISSION_GUARD_COUNT"
)
WIDGETS = (
    "root",
    "heading",
    "status",
    "detail",
)
OPTIONAL_WIDGETS = (
    "close_button",
    "close_icon",
    "previous_button",
    "next_button",
    "detail_view",
    "detail_action",
    "list_view",
    "empty_view",
    "empty_heading",
)


def distinct_widgets(config):
    names = [str(config[name]) for name in WIDGETS]
    names.extend(str(config[name]) for name in OPTIONAL_WIDGETS if name in config)
    names += [
        str(row[name])
        for row in config["rows"]
        for name in ("root", "title", "message")
    ]
    names += [
        str(row[name])
        for row in config["rows"]
        for name in ("source_label", "age_label")
        if name in row
    ]
    if len(names) != len(set(names)):
        raise cv.Invalid("Notification center widgets must be distinct")
    if config["history_scroll"]:
        if (
            "list_view" not in config
            or not config["row_pitch"]
            or not config["close_edge_pixels"]
        ):
            raise cv.Invalid(
                "Scrolling history requires list_view, row_pitch and a bottom close edge"
            )
    elif any(
        name not in config
        for name in ("close_button", "previous_button", "next_button")
    ):
        raise cv.Invalid("Button navigation requires close, previous and next buttons")
    if config["row_snapshot_cache"] and not config["history_scroll"]:
        raise cv.Invalid("Notification row snapshots require scrolling history")
    if "detail_action" in config and not config["history_scroll"]:
        raise cv.Invalid("Notification detail actions require scrolling history")
    return config


SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MaterialNotificationCenter),
        cv.GenerateID(CONF_LVGL_ID): cv.use_id(LvglComponent),
        cv.Required("navigation_id"): cv.use_id(LvglNavigation),
        cv.Required("banner_id"): cv.use_id(MaterialNotificationOverlay),
        cv.Required("admitted"): cv.returning_lambda,
        cv.Required("text_provider"): cv.returning_lambda,
        cv.Optional("close_glyph", default="x"): cv.All(
            cv.string_strict, cv.Length(min=1, max=1)
        ),
        cv.Optional("back_glyph", default="<"): cv.All(
            cv.string_strict, cv.Length(min=1, max=1)
        ),
        **{cv.Required(name): cv.use_id(lv_obj_t) for name in WIDGETS},
        **{cv.Optional(name): cv.use_id(lv_obj_t) for name in OPTIONAL_WIDGETS},
        cv.Optional("history_scroll", default=False): cv.boolean,
        cv.Optional("isolate_native_layers", default=False): cv.boolean,
        cv.Optional("cached_backdrop_dim", default=False): cv.boolean,
        cv.Optional("direct_presentation", default=False): cv.boolean,
        cv.Optional("backdrop_dim_shift", default=1): cv.int_range(min=1, max=2),
        cv.Optional("row_snapshot_cache", default=False): cv.boolean,
        cv.Optional("row_cache_band_count", default=1): cv.int_range(min=1, max=8),
        cv.Optional("row_cache_min_free_bytes", default=1048576): cv.int_range(
            min=0, max=0xFFFFFFFF
        ),
        cv.Optional("row_pitch", default=0): cv.int_range(min=0, max=2048),
        cv.Optional("close_edge_pixels", default=0): cv.int_range(min=0, max=256),
        cv.Optional("close_commit_pixels", default=36): cv.int_range(min=8, max=512),
        cv.Optional(
            "transition_duration", default="0ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional("now_provider"): cv.returning_lambda,
        cv.Required("rows"): cv.All(
            cv.ensure_list(
                cv.Schema(
                    {
                        cv.Required(name): cv.use_id(lv_obj_t)
                        for name in ("root", "title", "message")
                    }
                    | {
                        cv.Optional(name): cv.use_id(lv_obj_t)
                        for name in ("source_label", "age_label")
                    }
                )
            ),
            cv.Length(min=8, max=8),
        ),
        cv.Optional("edge_pixels", default=32): cv.int_range(min=1, max=256),
        cv.Optional("commit_pixels", default=60): cv.int_range(min=8, max=512),
        cv.Optional("dismiss_commit_pixels"): cv.int_range(min=9, max=512),
        cv.Required("scene_controllers"): cv.All(
            cv.ensure_list(cv.use_id(DirectSceneController)), cv.Length(max=4)
        ),
        cv.Optional("on_request"): automation.validate_automation({}),
    }
).extend(cv.COMPONENT_SCHEMA)
SCHEMA = cv.All(SCHEMA, distinct_widgets)


async def to_code(config):
    for conf in config:
        cg.add_define("USE_LVGL_MATERIAL_NOTIFICATION_CENTER")
        if "detail_action" in conf:
            cg.add_define("USE_LVGL_MATERIAL_NOTIFICATION_ACTION")
        cg.add_define("USE_LVGL_NAVIGATION_ADMISSION_GUARD")
        request_admission_slot(str(conf["navigation_id"]))
        parent = await cg.get_variable(conf[CONF_LVGL_ID])
        navigation = await cg.get_variable(conf["navigation_id"])
        banner = await cg.get_variable(conf["banner_id"])
        var = cg.new_Pvariable(conf[CONF_ID], parent, navigation, banner)
        await cg.register_component(var, conf)
        cg.add(var.set_edge_pixels(conf["edge_pixels"]))
        cg.add(var.set_commit_pixels(conf["commit_pixels"]))
        if "dismiss_commit_pixels" in conf:
            cg.add(var.set_dismiss_commit_pixels(conf["dismiss_commit_pixels"]))
        cg.add(var.set_history_scroll(conf["history_scroll"]))
        cg.add(var.set_isolate_native_layers(conf["isolate_native_layers"]))
        cg.add(var.set_cached_backdrop_dim(conf["cached_backdrop_dim"]))
        cg.add(var.set_direct_presentation(conf["direct_presentation"]))
        cg.add(var.set_backdrop_dim_shift(conf["backdrop_dim_shift"]))
        if conf["row_snapshot_cache"]:
            cg.add_define("USE_LVGL_MATERIAL_NOTIFICATION_ROW_CACHE")
            add_lv_use("SNAPSHOT", "IMAGE")
            cg.add(var.set_row_snapshot_cache(True))
            cg.add(var.set_row_cache_band_count(conf["row_cache_band_count"]))
            cg.add(var.set_row_cache_min_free_bytes(conf["row_cache_min_free_bytes"]))
        cg.add(var.set_row_pitch(conf["row_pitch"]))
        cg.add(var.set_close_edge_pixels(conf["close_edge_pixels"]))
        cg.add(var.set_close_commit_pixels(conf["close_commit_pixels"]))
        cg.add(var.set_transition_duration(conf["transition_duration"]))
        if "now_provider" in conf:
            cg.add(
                var.set_now_provider(
                    await cg.process_lambda(
                        conf["now_provider"], [], return_type=cg.uint32
                    )
                )
            )
        cg.add(var.set_close_glyph(conf["close_glyph"]))
        cg.add(var.set_back_glyph(conf["back_glyph"]))
        cg.add(
            var.set_text_provider(
                await cg.process_lambda(
                    conf["text_provider"],
                    [(cg.uint8, "text_id")],
                    return_type=cg.const_char_ptr,
                )
            )
        )
        cg.add(
            var.set_admission(
                await cg.process_lambda(conf["admitted"], [], return_type=cg.bool_)
            )
        )
        for controller in conf["scene_controllers"]:
            cg.add(var.add_scene_controller(await cg.get_variable(controller)))
        for automation_conf in conf.get("on_request", []):
            await automation.build_callback_automation(
                var,
                "add_on_request_callback",
                [(cg.std_string, "payload")],
                automation_conf,
            )
        widgets = [(name, (await get_widgets(conf, name))[0]) for name in WIDGETS]
        widgets += [
            (name, (await get_widgets(conf, name))[0])
            for name in OPTIONAL_WIDGETS
            if name in conf
        ]
        rows = [
            [(await get_widgets(row, name))[0] for name in ("root", "title", "message")]
            for row in conf["rows"]
        ]
        metadata = [
            [
                (await get_widgets(row, name))[0] if name in row else None
                for name in ("source_label", "age_label")
            ]
            for row in conf["rows"]
        ]
        async with LvContext() as ctx:
            for name, widget in widgets:
                ctx.add(getattr(var, f"set_{name}")(widget.obj))
            for index, row in enumerate(rows):
                ctx.add(var.set_row(index, *(widget.obj for widget in row)))
                ctx.add(
                    var.set_row_metadata(
                        index,
                        *(
                            widget.obj if widget is not None else cg.nullptr
                            for widget in metadata[index]
                        ),
                    )
                )


@automation.register_action(
    "lvgl_material.notifications.sync",
    SyncAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(MaterialNotificationCenter),
            cv.Required(CONF_PAYLOAD): cv.templatable(cv.string),
        }
    ),
    synchronous=True,
)
async def sync_to_code(config, action_id, template_arg, args):
    parent = await cg.get_variable(config[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    cg.add(
        var.set_payload(await cg.templatable(config[CONF_PAYLOAD], args, cg.std_string))
    )
    return var
