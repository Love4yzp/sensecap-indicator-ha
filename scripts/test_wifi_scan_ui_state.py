#!/usr/bin/env python3
"""Host tests for generation-safe Wi-Fi scan UI state."""

from __future__ import annotations

import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

#include "wifi_scan_ui_state.h"

int main(void)
{
    struct wifi_scan_ui_state state = {0};

    uint32_t hidden_scan = wifi_scan_ui_begin(&state);
    assert(hidden_scan != 0);
    wifi_scan_ui_hide(&state);
    assert(
        wifi_scan_ui_complete(&state, hidden_scan) ==
        WIFI_SCAN_UI_COMPLETION_DISCARD);
    assert(
        wifi_scan_ui_complete(&state, hidden_scan) ==
        WIFI_SCAN_UI_COMPLETION_STALE);

    uint32_t later_scan = wifi_scan_ui_begin(&state);
    assert(later_scan != hidden_scan);
    assert(
        wifi_scan_ui_complete(&state, later_scan) ==
        WIFI_SCAN_UI_COMPLETION_CURRENT);

    uint32_t old_scan = wifi_scan_ui_begin(&state);
    uint32_t current_scan = wifi_scan_ui_begin(&state);
    assert(old_scan != current_scan);
    assert(
        wifi_scan_ui_complete(&state, old_scan) ==
        WIFI_SCAN_UI_COMPLETION_STALE);
    assert(!wifi_scan_ui_timeout(&state, old_scan));
    assert(
        wifi_scan_ui_complete(&state, current_scan) ==
        WIFI_SCAN_UI_COMPLETION_CURRENT);

    uint32_t timed_out_scan = wifi_scan_ui_begin(&state);
    assert(wifi_scan_ui_timeout(&state, timed_out_scan));
    assert(!wifi_scan_ui_timeout(&state, timed_out_scan));

    return 0;
}
"""


class WifiScanUiStateTests(unittest.TestCase):
    def test_stale_completions_cannot_pollute_later_scans(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "wifi_scan_ui_state_test"
            harness = Path(tmp) / "wifi_scan_ui_state_test.c"
            harness.write_text(textwrap.dedent(HARNESS), encoding="utf-8")
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "main" / "wifi"),
                    str(harness),
                    str(ROOT / "main" / "wifi" / "wifi_scan_ui_state.c"),
                    "-o",
                    str(binary),
                ],
                cwd=ROOT,
                check=True,
            )
            subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=2)

    def test_view_routes_all_scan_completions_through_generation_state(self) -> None:
        source = (ROOT / "main" / "wifi" / "wifi_view.c").read_text(
            encoding="utf-8"
        )
        self.assertIn('#include "wifi_scan_ui_state.h"', source)
        self.assertNotIn("s_discard_next_list", source)
        self.assertIn("wifi_scan_ui_begin", source)
        self.assertIn("wifi_scan_ui_hide", source)
        self.assertIn("wifi_scan_ui_complete", source)
        self.assertIn("wifi_scan_ui_timeout", source)
        self.assertIn("scan_generation", source)


if __name__ == "__main__":
    unittest.main(verbosity=2)
