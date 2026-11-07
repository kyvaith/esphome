"""Tile events use the existing Action view and public LVGL pointer lifecycle."""

import json

from test_action_native_input import (
    ActionNativeInputTests,
    fixtures_header as action_header,
)
from test_tile_event_control import corpus

SCENARIOS = (
    "tile-press",
    "tile-readonly",
    "tile-source-aba",
    "tile-outside",
    "momentary",
    "dispatched",
)


def fixtures_header():
    entries = [
        "{"
        + json.dumps("tile-" + name)
        + ","
        + json.dumps(json.dumps(obj, ensure_ascii=False, separators=(",", ":")))
        + "}"
        for name, obj, valid in corpus()
        if valid and name in ("first-use", "read-only")
    ]
    return action_header().removesuffix("};") + ",\n" + ",\n".join(entries) + "};"


class TileEventNativeInputTests(ActionNativeInputTests):
    extra_features = ("TILE_EVENT_CONTROLS",)
    fixtures_header = staticmethod(fixtures_header)

    def test_profiles_observation_and_pointer_retirement(self):
        for case in SCENARIOS:
            with self.subTest(case=case):
                self.assertIn(
                    "RESULT " + case + " pass=1",
                    self.checked([self.executable, case]).stdout,
                )
