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
    def scan_findings(self, relative_path: str, source: str):
        findings = []
        scan_view_event_seam(ROOT / relative_path, source, findings)
        return findings

    def scan(self, relative_path: str, source: str) -> list[str]:
        return [
            finding.rule
            for finding in self.scan_findings(relative_path, source)
        ]

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

    def test_rejects_all_private_adapter_include_spellings_outside_module(self) -> None:
        for include in (
            '#include "view_event_adapter.h"\n',
            '#include "view_event/view_event_adapter.h"\n',
            '#include "../view_event/view_event_adapter.h"\n',
            "#include <view_event/view_event_adapter.h>\n",
        ):
            with self.subTest(include=include):
                rules = self.scan("main/sensor/example.c", include)
                self.assertEqual(rules, ["view-event-private-adapter"])

    def test_view_event_private_include_ignores_non_code_and_keeps_line_numbers(
        self,
    ) -> None:
        non_code = """\
// #include "view_event_adapter.h"
/* #include "../view_event/view_event_adapter.h" */
const char *example = "#include \\"view_event/view_event_adapter.h\\"";
"""
        self.assertNotIn(
            "view-event-private-adapter",
            self.scan("main/sensor/example.c", non_code),
        )

        source = """\
// #include "view_event_adapter.h"
const char *example = "#include \\"view_event_adapter.h\\"";

#include "../view_event/view_event_adapter.h"
"""
        findings = [
            finding
            for finding in self.scan_findings("main/sensor/example.c", source)
            if finding.rule == "view-event-private-adapter"
        ]
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0].line, 4)

    def test_rejects_ignored_command_delivery_result(self) -> None:
        for source in (
            "view_event_post_command(VIEW_EVENT_WIFI_LIST_REQ, NULL, 0);\n",
            "(void)view_event_post_command(VIEW_EVENT_WIFI_LIST_REQ, NULL, 0);\n",
            "if (ready) view_event_post_command("
            "VIEW_EVENT_WIFI_LIST_REQ, NULL, 0);\n",
            "if (ready)\n"
            "    view_event_post_command(VIEW_EVENT_WIFI_LIST_REQ, NULL, 0);\n",
            "int x = 0; view_event_post_command("
            "VIEW_EVENT_WIFI_LIST_REQ, NULL, 0);\n",
            "int x = 0, view_event_post_command("
            "VIEW_EVENT_WIFI_LIST_REQ, NULL, 0);\n",
        ):
            with self.subTest(source=source):
                rules = self.scan("main/wifi/example.c", source)
                self.assertIn("view-event-command-result", rules)

    def test_allows_explicit_command_delivery_handling(self) -> None:
        for source in (
            "return view_event_post_command(VIEW_EVENT_WIFI_LIST_REQ, NULL, 0);\n",
            "view_event_delivery_t delivery = view_event_post_command("
            "VIEW_EVENT_WIFI_LIST_REQ, NULL, 0);\n",
            "if (view_event_post_command(VIEW_EVENT_WIFI_LIST_REQ, NULL, 0) "
            "!= VIEW_EVENT_DELIVERY_ENQUEUED) {}\n",
            "if (\n"
            "    view_event_post_command(VIEW_EVENT_WIFI_LIST_REQ, NULL, 0)\n"
            "    != VIEW_EVENT_DELIVERY_ENQUEUED\n"
            ") {}\n",
        ):
            with self.subTest(source=source):
                self.assertNotIn(
                    "view-event-command-result",
                    self.scan("main/wifi/example.c", source),
                )

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

    def test_rejects_wifi_scan_private_adapter_outside_worker_module(self) -> None:
        for include in (
            '#include "wifi_scan_worker_adapter.h"\n',
            '#include "wifi/wifi_scan_worker_adapter.h"\n',
            '#include "../wifi/wifi_scan_worker_adapter.h"\n',
            "#include <wifi/wifi_scan_worker_adapter.h>\n",
        ):
            with self.subTest(include=include):
                rules = self.scan("main/ha/example.c", include)
                self.assertIn("wifi-scan-private-adapter", rules)

        for path in (
            "main/wifi/wifi_scan_worker.c",
            "main/wifi/wifi_scan_worker_esp_adapter.c",
        ):
            with self.subTest(path=path):
                self.assertNotIn(
                    "wifi-scan-private-adapter",
                    self.scan(path, '#include "wifi_scan_worker_adapter.h"\n'),
                )

    def test_wifi_scan_private_include_ignores_non_code_and_keeps_line_numbers(
        self,
    ) -> None:
        non_code = """\
// #include "wifi_scan_worker_adapter.h"
/* #include "../wifi/wifi_scan_worker_adapter.h" */
const char *example = "#include \\"wifi/wifi_scan_worker_adapter.h\\"";
"""
        self.assertNotIn(
            "wifi-scan-private-adapter",
            self.scan("main/ha/example.c", non_code),
        )

        source = """\
// #include "wifi_scan_worker_adapter.h"
const char *example = "#include \\"wifi_scan_worker_adapter.h\\"";

#include "../wifi/wifi_scan_worker_adapter.h"
"""
        findings = [
            finding
            for finding in self.scan_findings("main/ha/example.c", source)
            if finding.rule == "wifi-scan-private-adapter"
        ]
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0].line, 4)


if __name__ == "__main__":
    unittest.main()
