#!/usr/bin/env python3
"""Host behavior and integration tests for LVGL health diagnostics."""

from __future__ import annotations

import subprocess
import tempfile
import textwrap
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


class LvglHealthTests(unittest.TestCase):
    def test_stall_escalation_and_recovery_are_edge_triggered(self) -> None:
        harness = r"""
        #include "lvgl_health_state.h"

        #include <assert.h>

        int main(void)
        {
            lvgl_health_state_t state;
            lvgl_health_state_init(&state, 1000);

            assert(
                lvgl_health_state_observe(&state, 3999, 0) ==
                LVGL_HEALTH_ACTION_NONE);
            assert(
                lvgl_health_state_observe(&state, 4000, 0) ==
                LVGL_HEALTH_ACTION_STALLED);
            assert(
                lvgl_health_state_observe(&state, 5000, 0) ==
                LVGL_HEALTH_ACTION_NONE);
            assert(
                lvgl_health_state_observe(&state, 31000, 0) ==
                LVGL_HEALTH_ACTION_ESCALATED);
            assert(
                lvgl_health_state_observe(&state, 31500, 1) ==
                LVGL_HEALTH_ACTION_RECOVERED);
            assert(
                lvgl_health_state_observe(&state, 31501, 1) ==
                LVGL_HEALTH_ACTION_NONE);
            assert(
                lvgl_health_state_observe(&state, 34500, 1) ==
                LVGL_HEALTH_ACTION_STALLED);
            return 0;
        }
        """
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            source = tmp_path / "lvgl_health_harness.c"
            binary = tmp_path / "lvgl_health_harness"
            source.write_text(textwrap.dedent(harness), encoding="utf-8")
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT / "main" / "lvgl_health"),
                    str(ROOT / "main" / "lvgl_health" / "lvgl_health_state.c"),
                    str(source),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True, timeout=2)

    def test_runtime_captures_the_actual_lvgl_task_and_never_reboots(self) -> None:
        runtime = (
            ROOT / "main" / "lvgl_health" / "lvgl_health.c"
        ).read_text(encoding="utf-8")
        port = (ROOT / "main" / "lv_port.c").read_text(encoding="utf-8")
        cmake = (ROOT / "main" / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("xTaskGetCurrentTaskHandle()", runtime)
        self.assertIn("lvgl_health_heartbeat()", port)
        self.assertIn("lv_timer_create", port)
        self.assertIn("uxTaskGetStackHighWaterMark", runtime)
        self.assertIn("eTaskGetState", runtime)
        self.assertNotIn("xTaskGetHandle", runtime)
        self.assertNotIn('"taskLVGL"', runtime)
        self.assertNotIn("esp_restart", runtime)
        self.assertIn("lvgl_health_start()", port)
        self.assertIn('"lvgl_health/lvgl_health.c"', cmake)
        self.assertIn('"lvgl_health/lvgl_health_state.c"', cmake)


if __name__ == "__main__":
    unittest.main(verbosity=2)
