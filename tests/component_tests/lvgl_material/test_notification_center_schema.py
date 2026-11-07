"""Buttonless scrolling is opt-in and keeps legacy component configurations valid."""

import copy
import unittest

from esphome.components.lvgl_material.notification_center import SCHEMA
import esphome.config_validation as cv
from esphome.core import Lambda


def configuration():
    return {
        "id": "center",
        "lvgl_id": "display_ui",
        "navigation_id": "navigation",
        "banner_id": "banner",
        "root": "root",
        "heading": "heading",
        "status": "status",
        "detail": "detail",
        "admitted": Lambda("return true;"),
        "text_provider": Lambda('return "Text";'),
        "scene_controllers": [],
        "rows": [
            {
                "root": f"row_{i}",
                "title": f"title_{i}",
                "message": f"message_{i}",
                "source_label": f"source_{i}",
                "age_label": f"age_{i}",
            }
            for i in range(8)
        ],
    }


class NotificationCenterSchemaTests(unittest.TestCase):
    def test_legacy_buttons_keep_conservative_defaults(self):
        data = configuration()
        data.update(
            close_button="button_close",
            previous_button="button_previous",
            next_button="button_next",
        )
        result = SCHEMA(data)
        self.assertFalse(result["history_scroll"])
        self.assertFalse(result["isolate_native_layers"])
        self.assertFalse(result["cached_backdrop_dim"])
        self.assertFalse(result["row_snapshot_cache"])
        self.assertEqual(result["row_cache_band_count"], 1)
        self.assertEqual(result["close_edge_pixels"], 0)
        self.assertEqual(result["transition_duration"].total_milliseconds, 0)

    def test_scroll_mode_needs_viewport_pitch_and_close_gesture(self):
        data = configuration()
        data.update(
            history_scroll=True, list_view="list", row_pitch=188, close_edge_pixels=50
        )
        result = SCHEMA(data)
        self.assertNotIn("close_button", result)
        for key in ("list_view", "row_pitch", "close_edge_pixels"):
            broken = copy.deepcopy(data)
            del broken[key]
            with self.subTest(missing=key), self.assertRaises(cv.Invalid):
                SCHEMA(broken)

    def test_metadata_and_viewport_ids_cannot_alias_other_widgets(self):
        data = configuration()
        data.update(
            history_scroll=True, list_view="list", row_pitch=188, close_edge_pixels=50
        )
        data["rows"][1]["age_label"] = "title_0"
        with self.assertRaises(cv.Invalid):
            SCHEMA(data)

    def test_row_cache_requires_scrolling_and_keeps_a_memory_reserve(self):
        data = configuration()
        data.update(
            close_button="button_close",
            previous_button="button_previous",
            next_button="button_next",
            row_snapshot_cache=True,
        )
        with self.assertRaises(cv.Invalid):
            SCHEMA(data)
        data.update(
            history_scroll=True, list_view="list", row_pitch=188, close_edge_pixels=50
        )
        self.assertEqual(SCHEMA(data)["row_cache_min_free_bytes"], 1048576)
        data["row_cache_min_free_bytes"] = -1
        with self.assertRaises(cv.Invalid):
            SCHEMA(data)

    def test_row_cache_band_count_is_bounded(self):
        data = configuration()
        data.update(
            history_scroll=True,
            list_view="list",
            row_pitch=188,
            close_edge_pixels=50,
            row_snapshot_cache=True,
        )
        for count in (1, 2, 8):
            data["row_cache_band_count"] = count
            self.assertEqual(SCHEMA(data)["row_cache_band_count"], count)
        for count in (0, 9):
            data["row_cache_band_count"] = count
            with self.assertRaises(cv.Invalid):
                SCHEMA(data)
