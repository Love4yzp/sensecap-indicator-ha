#!/usr/bin/env python3
"""Regression checks for the RGB LCD + esp_lvgl_port integration."""

from __future__ import annotations

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parent.parent
BSP_LCD = ROOT / "components/bsp/src/peripherals/bsp_lcd.c"
LV_PORT = ROOT / "main/lv_port.c"
SDKCONFIG_DEFAULTS = ROOT / "sdkconfig.defaults"


class LcdRgbConfigTests(unittest.TestCase):
    def test_rgb_panel_does_not_force_refresh_on_demand(self) -> None:
        text = BSP_LCD.read_text()

        self.assertNotIn(
            ".flags.refresh_on_demand = 1",
            text,
            "esp_lvgl_port owns RGB refresh now; forcing refresh_on_demand "
            "without its own refresh task can leave the screen stale.",
        )

    def test_bsp_does_not_own_rgb_refresh_loop(self) -> None:
        text = BSP_LCD.read_text()

        self.assertNotIn("esp_lcd_rgb_panel_refresh(", text)
        self.assertNotIn("xTaskCreate(lcd_task", text)
        self.assertNotIn("esp_lcd_rgb_panel_register_event_callbacks", text)

    def test_lvgl_task_stack_is_large_enough_for_lvgl9_rgb(self) -> None:
        text = LV_PORT.read_text()
        match = re.search(r"#define\s+LV_PORT_TASK_STACK_SIZE\s+\((\d+)\)", text)

        self.assertIsNotNone(match)
        assert match is not None
        self.assertGreaterEqual(int(match.group(1)), 8192)

    def test_avoid_tear_uses_full_refresh_not_direct_mode(self) -> None:
        text = SDKCONFIG_DEFAULTS.read_text()

        self.assertIn("CONFIG_LCD_LVGL_FULL_REFRESH=y", text)
        self.assertNotIn("CONFIG_LCD_LVGL_DIRECT_MODE=y", text)

    def test_lvgl_refresh_period_is_fast_enough_for_swipe_motion(self) -> None:
        text = SDKCONFIG_DEFAULTS.read_text()

        self.assertIn("CONFIG_LV_DEF_REFR_PERIOD=10", text)

    def test_rgb_panel_uses_bounce_buffer_for_psram_scanout(self) -> None:
        text = BSP_LCD.read_text()

        self.assertIn(".bounce_buffer_size_px = brd->LCD_WIDTH * 10", text)

    def test_lvgl_port_uses_bounce_buffer_completion_callback(self) -> None:
        text = LV_PORT.read_text()

        self.assertIn(".bb_mode = true", text)

    def test_backlight_sleep_keeps_ledc_pwm_restartable(self) -> None:
        text = (ROOT / "main/display/display_model.c").read_text()
        start = text.index("static void _lcd_bl_off")
        end = text.index("static void _sleep_mode_timer_callback")
        body = text[start:end]

        self.assertNotIn("ledc_stop(", body)
        self.assertIn("ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0)", body)
        self.assertIn("ledc_update_duty(LEDC_MODE, LEDC_CHANNEL)", body)
        self.assertIn("_display_st_set(false)", body)


if __name__ == "__main__":
    unittest.main(verbosity=2)
