"""Negotiated Cover facets retain the existing owner and Stop fence."""

from pathlib import Path
import unittest
from unittest.mock import patch

import test_cover_priority_stop as priority


class CoverPanelTests(unittest.TestCase):
    def test_fenced_axes_strict_wire_and_legacy_negotiation(self):
        compiler = priority.compiler_command
        harness = (
            Path(__file__)
            .with_name("cover_panel_owner_test.cpp")
            .read_text(encoding="utf-8")
        )
        with (
            patch.object(priority, "HARNESS", harness),
            patch.object(
                priority,
                "compiler_command",
                lambda: [*compiler(), "-DUSE_LVGL_MATERIAL_COVER_PANEL"],
            ),
        ):
            priority.CoverPriorityOwnerTests().test_exact_channel_pending_stop_late_result_and_deadline()
