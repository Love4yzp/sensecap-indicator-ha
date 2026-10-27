#!/usr/bin/env python3
"""Unit tests for architecture-scan source masking."""

from __future__ import annotations

import unittest

from architecture_scan import (
    LVGL_CALL_RE,
    ROOT,
    mask_c_non_code,
    scan_view_event_seam,
)


class MaskCNonCodeTests(unittest.TestCase):
    def test_masks_comments_and_literals_but_keeps_real_calls(self) -> None:
        source = """\
/* lv_label_set_text(blocked, "comment"); */
const char *example = "lv_obj_create(fake)";
// lv_button_create(commented);
lv_obj_create(real);
"""

        masked = mask_c_non_code(source)
        calls = [match.group(1) for match in LVGL_CALL_RE.finditer(masked)]

        self.assertEqual(calls, ["lv_obj_create"])
        self.assertEqual(len(masked), len(source))
        self.assertEqual(masked.count("\n"), source.count("\n"))

    def test_handles_escaped_quotes(self) -> None:
        source = 'const char *value = "escaped \\" lv_obj_create(fake)";\nlv_label_create(real);\n'

        masked = mask_c_non_code(source)
        calls = [match.group(1) for match in LVGL_CALL_RE.finditer(masked)]

        self.assertEqual(calls, ["lv_label_create"])


class ViewEventSeamTests(unittest.TestCase):
    def scan(self, relative_path: str, source: str) -> list[str]:
        findings = []
        scan_view_event_seam(ROOT / relative_path, source, findings)
        return [finding.rule for finding in findings]

    def test_rejects_legacy_direct_transport(self) -> None:
        rules = self.scan(
            "main/wifi/example.c",
            """\
esp_event_post_to(
    view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST,
    &state, sizeof(state), portMAX_DELAY);
""",
        )

        self.assertIn("view-event-legacy-transport", rules)

    def test_rejects_private_adapter_include_outside_module(self) -> None:
        rules = self.scan(
            "main/sensor/example.c",
            '#include "view_event_adapter.h"\n',
        )

        self.assertEqual(rules, ["view-event-private-adapter"])

    def test_allows_esp_transport_only_in_production_adapter(self) -> None:
        source = """\
ESP_EVENT_DEFINE_BASE(VIEW_EVENT_TRANSPORT_BASE);
esp_event_post_to(loop, VIEW_EVENT_TRANSPORT_BASE, 0, NULL, 0, 0);
"""
        adapter_rules = self.scan(
            "main/view_event/view_event_esp_adapter.c", source
        )
        core_rules = self.scan("main/view_event/view_event.c", source)

        self.assertEqual(adapter_rules, [])
        self.assertIn("view-event-internal-transport", core_rules)
        self.assertIn("view-event-adapter-boundary", core_rules)


if __name__ == "__main__":
    unittest.main()
