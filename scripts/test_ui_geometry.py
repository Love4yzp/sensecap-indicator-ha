#!/usr/bin/env python3
"""Regression checks for fullscreen LVGL UI geometry."""

from __future__ import annotations

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parent.parent
MAIN = ROOT / "main"
NAV = MAIN / "nav/nav.c"
NAV_HEADER = MAIN / "nav/nav.h"
WIFI_VIEW = MAIN / "wifi/wifi_view.c"
SETTINGS_VIEW = MAIN / "settings/settings_view.c"


class UiGeometryTests(unittest.TestCase):
    def test_no_fullscreen_container_uses_stale_800px_height(self) -> None:
        offenders: list[str] = []
        for path in MAIN.rglob("*.c"):
            text = path.read_text()
            if "480, 800" in text or "#define NAV_TILE_HEIGHT 800" in text:
                offenders.append(str(path.relative_to(ROOT)))

        self.assertEqual([], offenders)

    def test_nav_uses_configured_screen_geometry_and_dark_theme(self) -> None:
        text = NAV.read_text()

        self.assertIn("#define NAV_TILE_WIDTH  CONFIG_LCD_EVB_SCREEN_WIDTH", text)
        self.assertIn("#define NAV_TILE_HEIGHT CONFIG_LCD_EVB_SCREEN_HEIGHT", text)
        self.assertIn("lv_theme_default_init", text)
        self.assertIn("lv_display_set_theme", text)

    def test_tileview_remains_scrollable_for_touch_swipes(self) -> None:
        text = NAV.read_text()

        self.assertIn("nav_style_base_obj(s_tileview)", text)
        self.assertNotIn("nav_style_static_obj(s_tileview)", text)

    def test_tileview_disables_elastic_offset_without_blocking_edge_swipes(self) -> None:
        text = NAV.read_text()

        self.assertIn("LV_OBJ_FLAG_SCROLL_ELASTIC", text)
        self.assertIn("LV_OBJ_FLAG_SCROLL_MOMENTUM", text)
        self.assertIn("LV_OBJ_FLAG_SCROLL_CHAIN", text)
        self.assertIn("LV_DIR_LEFT | LV_DIR_RIGHT", text)
        self.assertNotIn("nav_tile_scroll_dir", text)

    def test_settings_is_gear_modal_not_fourth_tile(self) -> None:
        nav_header = NAV_HEADER.read_text()
        settings_text = SETTINGS_VIEW.read_text()

        self.assertIn("#define NAV_TILE_COUNT    3", nav_header)
        self.assertNotIn("NAV_TILE_SETTINGS", nav_header)
        self.assertIn("LV_SYMBOL_SETTINGS", settings_text)
        self.assertIn("lv_obj_set_align(button, LV_ALIGN_TOP_LEFT)", settings_text)
        self.assertIn("lv_obj_add_flag(s_settings_modal, LV_OBJ_FLAG_CLICKABLE)", settings_text)
        self.assertIn("LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE", settings_text)
        self.assertIn("SCREEN_DISPLAY_MODAL", settings_text)
        self.assertIn("SCREEN_BROKER_MODAL", settings_text)

    def test_settings_modal_preserves_subscreen_back_stack_and_shows_ip(self) -> None:
        settings_text = SETTINGS_VIEW.read_text()

        self.assertIn('#include "esp_netif.h"', settings_text)
        self.assertIn("static lv_obj_t *s_settings_ip_label = NULL;", settings_text)
        self.assertIn("settings_refresh_ip_label();", settings_text)
        self.assertIn('esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")', settings_text)
        self.assertIn('"IP: not connected"', settings_text)

        post_screen_start = settings_text.index("static void settings_post_screen")
        post_screen_end = settings_text.index("static void settings_open_wifi")
        post_screen_body = settings_text[post_screen_start:post_screen_end]
        self.assertNotIn("settings_hide_modal();", post_screen_body)
        self.assertIn("esp_event_post_to", post_screen_body)

    def test_temperature_unit_uses_product_degree_symbol(self) -> None:
        offenders: list[str] = []
        for path in MAIN.rglob("*.c"):
            text = path.read_text()
            if "deg C" in text:
                offenders.append(str(path.relative_to(ROOT)))

        self.assertEqual([], offenders)

    def test_wifi_back_button_uses_touch_friendly_modal_pattern(self) -> None:
        text = WIFI_VIEW.read_text()

        self.assertIn("lv_obj_move_foreground(s_wifi_modal)", text)
        self.assertIn("lv_obj_add_flag(s_wifi_modal, LV_OBJ_FLAG_CLICKABLE)", text)
        self.assertIn("LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE", text)
        self.assertIn("lv_obj_set_size(back, 100, 50)", text)
        self.assertIn("lv_obj_set_pos(back, 10, 17)", text)
        self.assertIn("lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP", text)
        self.assertIn("LV_STATE_PRESSED", text)
        self.assertIn('lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back")', text)

    def test_wifi_back_during_scan_discards_late_scan_result(self) -> None:
        text = WIFI_VIEW.read_text()

        self.assertIn("static bool s_discard_next_list = false;", text)
        self.assertIn("static bool s_wifi_scan_pending = false;", text)
        self.assertIn("s_wifi_scan_pending = true;", text)
        self.assertIn("s_wifi_scan_pending = false;", text)
        self.assertIn("discard stale scan result", text)

        hide_start = text.index("static void _hide_wifi_modal")
        hide_end = text.index("static void _on_wifi_modal_back")
        hide_body = text[hide_start:hide_end]
        self.assertIn("if(s_wifi_scan_pending)", hide_body)
        self.assertIn("s_discard_next_list = true;", hide_body)

        list_case = text[text.index("case VIEW_EVENT_WIFI_LIST:"):]
        self.assertLess(
            list_case.index("if(s_discard_next_list)"),
            list_case.index("wifi_list_screen_update"),
        )


if __name__ == "__main__":
    unittest.main(verbosity=2)
