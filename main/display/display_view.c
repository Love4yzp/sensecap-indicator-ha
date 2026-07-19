/**
 * @file view_display.c
 * @brief
 * Adjust screen brightness and display time
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "display_model.h"
#include "display_view.h"
#include "lv_port.h"
#include "view_data.h"
#include <esp_log.h>
#include "sdkconfig.h"

static const char* TAG = "display-view";

static lv_obj_t* s_display_modal = NULL;
static lv_obj_t* s_brightness_cfg = NULL;
static lv_obj_t* s_sleep_mode_cfg = NULL;
static lv_obj_t* s_sleep_mode_time_panel = NULL;
static lv_obj_t* s_sleep_mode_time_cfg = NULL;
static lv_obj_t* s_display_keyboard = NULL;

static void _apply_display_cfg_to_widgets(const struct view_data_display* cfg);
static void _ensure_display_modal(void);

static void _sync_sleep_time_panel(void) {
	if(!s_sleep_mode_cfg || !s_sleep_mode_time_panel)
	{
		return;
	}

	if(lv_obj_has_state(s_sleep_mode_cfg, LV_STATE_CHECKED))
	{
		lv_obj_remove_flag(s_sleep_mode_time_panel, LV_OBJ_FLAG_HIDDEN);
	}
	else
	{
		lv_obj_add_flag(s_sleep_mode_time_panel, LV_OBJ_FLAG_HIDDEN);
		if(s_display_keyboard)
		{
			lv_obj_add_flag(s_display_keyboard, LV_OBJ_FLAG_HIDDEN);
		}
	}
}

static void _display_cfg_from_widgets(struct view_data_display* cfg) {
	_display_cfg_get(cfg);

	if(s_brightness_cfg)
	{
		cfg->brightness = lv_slider_get_value(s_brightness_cfg);
	}

	cfg->sleep_mode_en = s_sleep_mode_cfg &&
		lv_obj_has_state(s_sleep_mode_cfg, LV_STATE_CHECKED);

	const char* p_time = s_sleep_mode_time_cfg ?
		lv_textarea_get_text(s_sleep_mode_time_cfg) : NULL;
	cfg->sleep_mode_time_min = (cfg->sleep_mode_en && p_time) ? atoi(p_time) : 0;
}

static void _hide_display_modal(void) {
	if(s_display_keyboard)
	{
		lv_obj_add_flag(s_display_keyboard, LV_OBJ_FLAG_HIDDEN);
	}
	if(s_display_modal)
	{
		lv_obj_add_flag(s_display_modal, LV_OBJ_FLAG_HIDDEN);
	}
}

static void _on_display_close(lv_event_t* e) {
	if(lv_event_get_code(e) != LV_EVENT_CLICKED)
	{
		return;
	}

	display_cfg_apply_event_cb(e);
	_hide_display_modal();
}

static void _on_sleep_mode_changed(lv_event_t* e) {
	if(lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED)
	{
		return;
	}

	_sync_sleep_time_panel();
	display_cfg_apply_event_cb(e);
}

static void _on_sleep_time_clicked(lv_event_t* e) {
	if(lv_event_get_code(e) != LV_EVENT_CLICKED || !s_display_keyboard)
	{
		return;
	}

	lv_obj_remove_flag(s_display_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void _on_keyboard_done(lv_event_t* e) {
	lv_event_code_t code = lv_event_get_code(e);
	if(code != LV_EVENT_READY && code != LV_EVENT_CANCEL && code != LV_EVENT_DEFOCUSED)
	{
		return;
	}

	if(s_display_keyboard)
	{
		lv_obj_add_flag(s_display_keyboard, LV_OBJ_FLAG_HIDDEN);
	}
	display_cfg_apply_event_cb(e);
}

static void _ensure_display_modal(void) {
	if(s_display_modal)
	{
		return;
	}

	s_display_modal = lv_obj_create(lv_layer_top());
	lv_obj_set_size(s_display_modal, CONFIG_LCD_EVB_SCREEN_WIDTH, CONFIG_LCD_EVB_SCREEN_HEIGHT);
	lv_obj_set_align(s_display_modal, LV_ALIGN_CENTER);
	lv_obj_add_flag(s_display_modal, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_remove_flag(s_display_modal, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
	lv_obj_set_style_bg_color(s_display_modal, lv_color_hex(0x101418),
							  LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(s_display_modal, LV_OPA_COVER,
							LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_width(s_display_modal, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_radius(s_display_modal, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_pad_all(s_display_modal, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_add_flag(s_display_modal, LV_OBJ_FLAG_HIDDEN);

	lv_obj_t* header = lv_obj_create(s_display_modal);
	lv_obj_set_size(header, 480, 85);
	lv_obj_set_align(header, LV_ALIGN_TOP_MID);
	lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP,
							LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_opa(header, LV_OPA_TRANSP,
								LV_PART_MAIN | LV_STATE_DEFAULT);

	lv_obj_t* back = lv_button_create(header);
	lv_obj_set_size(back, 100, 50);
	lv_obj_set_pos(back, 10, 17);
	lv_obj_set_style_bg_opa(back, LV_OPA_TRANSP,
							  LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(back, lv_color_hex(0x2a3036),
							  LV_PART_MAIN | LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(back, LV_OPA_40,
							LV_PART_MAIN | LV_STATE_PRESSED);
	lv_obj_set_style_border_width(back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_radius(back, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_pad_all(back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_shadow_width(back, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_add_event_cb(back, _on_display_close, LV_EVENT_CLICKED, NULL);
	lv_obj_t* back_label = lv_label_create(back);
	lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
	lv_obj_set_style_text_color(back_label, lv_color_hex(0xe7ecef),
								LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_center(back_label);

	lv_obj_t* title = lv_label_create(header);
	lv_label_set_text(title, "Display");
	lv_obj_set_style_text_color(title, lv_color_white(),
								LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_align(title, LV_ALIGN_BOTTOM_MID);

	lv_obj_t* brightness_panel = lv_obj_create(s_display_modal);
	lv_obj_set_size(brightness_panel, 400, 100);
	lv_obj_set_pos(brightness_panel, 40, 150);
	lv_obj_remove_flag(brightness_panel, LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t* brightness_title = lv_label_create(brightness_panel);
	lv_label_set_text(brightness_title, "Brightness");
	lv_obj_set_align(brightness_title, LV_ALIGN_TOP_LEFT);

	s_brightness_cfg = lv_slider_create(brightness_panel);
	lv_slider_set_range(s_brightness_cfg, 1, 100);
	lv_obj_set_size(s_brightness_cfg, 250, 10);
	lv_obj_set_align(s_brightness_cfg, LV_ALIGN_CENTER);
	lv_obj_set_y(s_brightness_cfg, 10);
	lv_obj_set_style_bg_color(s_brightness_cfg, lv_color_hex(0x363636),
							  LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(s_brightness_cfg, lv_color_hex(0x529D53),
							  LV_PART_INDICATOR | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(s_brightness_cfg, lv_color_white(),
							  LV_PART_KNOB | LV_STATE_DEFAULT);
	lv_obj_add_event_cb(s_brightness_cfg, brighness_cfg_event_cb,
						LV_EVENT_VALUE_CHANGED, NULL);

	lv_obj_t* low_label = lv_label_create(brightness_panel);
	lv_label_set_text(low_label, "-");
	lv_obj_set_align(low_label, LV_ALIGN_LEFT_MID);
	lv_obj_set_y(low_label, 10);

	lv_obj_t* high_label = lv_label_create(brightness_panel);
	lv_label_set_text(high_label, "+");
	lv_obj_set_align(high_label, LV_ALIGN_RIGHT_MID);
	lv_obj_set_y(high_label, 10);

	lv_obj_t* sleep_panel = lv_obj_create(s_display_modal);
	lv_obj_set_size(sleep_panel, 400, 50);
	lv_obj_set_pos(sleep_panel, 40, 270);
	lv_obj_remove_flag(sleep_panel, LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t* sleep_title = lv_label_create(sleep_panel);
	lv_label_set_text(sleep_title, "Sleep Mode");
	lv_obj_set_align(sleep_title, LV_ALIGN_LEFT_MID);

	s_sleep_mode_cfg = lv_switch_create(sleep_panel);
	lv_obj_set_size(s_sleep_mode_cfg, 50, 25);
	lv_obj_set_align(s_sleep_mode_cfg, LV_ALIGN_RIGHT_MID);
	lv_obj_set_style_bg_color(s_sleep_mode_cfg, lv_color_hex(0x363636),
							  LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(s_sleep_mode_cfg, lv_color_hex(0x529D53),
							  LV_PART_INDICATOR | LV_STATE_CHECKED);
	lv_obj_add_event_cb(s_sleep_mode_cfg, _on_sleep_mode_changed,
						LV_EVENT_VALUE_CHANGED, NULL);

	s_sleep_mode_time_panel = lv_obj_create(s_display_modal);
	lv_obj_set_size(s_sleep_mode_time_panel, 400, 50);
	lv_obj_set_pos(s_sleep_mode_time_panel, 40, 335);
	lv_obj_remove_flag(s_sleep_mode_time_panel, LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t* after_label = lv_label_create(s_sleep_mode_time_panel);
	lv_label_set_text(after_label, "After");
	lv_obj_set_align(after_label, LV_ALIGN_LEFT_MID);

	s_sleep_mode_time_cfg = lv_textarea_create(s_sleep_mode_time_panel);
	lv_obj_set_size(s_sleep_mode_time_cfg, 80, 40);
	lv_obj_set_align(s_sleep_mode_time_cfg, LV_ALIGN_RIGHT_MID);
	lv_obj_set_x(s_sleep_mode_time_cfg, -50);
	lv_textarea_set_accepted_chars(s_sleep_mode_time_cfg, "0123456789");
	lv_textarea_set_max_length(s_sleep_mode_time_cfg, 4);
	lv_textarea_set_placeholder_text(s_sleep_mode_time_cfg, "1");
	lv_textarea_set_one_line(s_sleep_mode_time_cfg, true);
	lv_obj_set_style_bg_color(s_sleep_mode_time_cfg, lv_color_hex(0x6F6F6F),
							  LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_add_event_cb(s_sleep_mode_time_cfg, _on_sleep_time_clicked,
						LV_EVENT_CLICKED, NULL);
	lv_obj_add_event_cb(s_sleep_mode_time_cfg, _on_keyboard_done,
						LV_EVENT_DEFOCUSED, NULL);

	lv_obj_t* min_label = lv_label_create(s_sleep_mode_time_panel);
	lv_label_set_text(min_label, "min");
	lv_obj_set_align(min_label, LV_ALIGN_RIGHT_MID);

	s_display_keyboard = lv_keyboard_create(s_display_modal);
	lv_keyboard_set_mode(s_display_keyboard, LV_KEYBOARD_MODE_NUMBER);
	lv_keyboard_set_textarea(s_display_keyboard, s_sleep_mode_time_cfg);
	lv_obj_set_size(s_display_keyboard, 480, 240);
	lv_obj_set_align(s_display_keyboard, LV_ALIGN_BOTTOM_MID);
	lv_obj_add_flag(s_display_keyboard, LV_OBJ_FLAG_HIDDEN);
	lv_obj_add_event_cb(s_display_keyboard, _on_keyboard_done,
						LV_EVENT_READY, NULL);
	lv_obj_add_event_cb(s_display_keyboard, _on_keyboard_done,
						LV_EVENT_CANCEL, NULL);

	struct view_data_display cfg;
	_display_cfg_get(&cfg);
	_apply_display_cfg_to_widgets(&cfg);
}

static void _apply_display_cfg_to_widgets(const struct view_data_display* cfg) {
	if(!cfg || !s_display_modal)
	{
		return;
	}

	if(s_brightness_cfg)
	{
		lv_slider_set_value(s_brightness_cfg, cfg->brightness, LV_ANIM_OFF);
	}

	if(s_sleep_mode_cfg)
	{
		if(cfg->sleep_mode_en)
		{
			lv_obj_add_state(s_sleep_mode_cfg, LV_STATE_CHECKED);
		}
		else
		{
			lv_obj_remove_state(s_sleep_mode_cfg, LV_STATE_CHECKED);
		}
	}

	if(s_sleep_mode_time_cfg)
	{
		char str[16] = {0};
		snprintf(str, sizeof(str), "%d", cfg->sleep_mode_time_min);
		lv_textarea_set_text(s_sleep_mode_time_cfg, str);
	}

	_sync_sleep_time_panel();
}

static void _show_display_modal(void) {
	_ensure_display_modal();
	if(!s_display_modal)
	{
		return;
	}

	struct view_data_display cfg;
	_display_cfg_get(&cfg);
	_apply_display_cfg_to_widgets(&cfg);
	lv_obj_remove_flag(s_display_modal, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(s_display_modal);
}

// static void _brighness_cfg_event_cb(lv_event_t * e)
void brighness_cfg_event_cb(lv_event_t* e) // Value changed
{
	lv_obj_t* slider = lv_event_get_target_obj(e);
	int32_t value = lv_slider_get_value(slider);
	/* LVGL task context: bound the post so a full view queue cannot freeze it. */
	esp_err_t err = esp_event_post_to(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_BRIGHTNESS_UPDATE,
		&value, sizeof(value), pdMS_TO_TICKS(100));
	if(err != ESP_OK)
	{
		ESP_LOGW(TAG, "drop VIEW_EVENT_BRIGHTNESS_UPDATE: %s", esp_err_to_name(err));
	}
}

// static void _display_cfg_apply_event_cb(lv_event_t * e)
void display_cfg_apply_event_cb(lv_event_t* e) // defocused the textarea
{
	struct view_data_display cfg;
	memset(&cfg, 0, sizeof(cfg));
	if(!s_brightness_cfg || !s_sleep_mode_cfg || !s_sleep_mode_time_cfg)
	{
		return;
	}
	_display_cfg_from_widgets(&cfg);
	/* LVGL task context: bound the post so a full view queue cannot freeze it. */
	esp_err_t err = esp_event_post_to(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_DISPLAY_CFG_APPLY,
		&cfg, sizeof(cfg), pdMS_TO_TICKS(100));
	if(err != ESP_OK)
	{
		ESP_LOGW(TAG, "drop VIEW_EVENT_DISPLAY_CFG_APPLY: %s", esp_err_to_name(err));
	}
}

void brighness_update_callback(lv_event_t* e) {
	struct view_data_display cfg;
	_display_cfg_get(&cfg);
	_ensure_display_modal();
	_apply_display_cfg_to_widgets(&cfg);
}

static void _view_event_handler(void* handler_args, esp_event_base_t base,
								int32_t id, void* event_data) {
	switch(id)
	{
		case VIEW_EVENT_SCREEN_START:
		{
			if(!event_data)
			{
				break;
			}
			uint8_t screen = *(uint8_t*)event_data;
			if(screen == SCREEN_DISPLAY_MODAL)
			{
				lv_port_sem_take();
				_show_display_modal();
				lv_port_sem_give();
			}
			break;
		}
		case VIEW_EVENT_DISPLAY_CFG:
		{
			if(!event_data)
			{
				break;
			}
			struct view_data_display* cfg = (struct view_data_display*)event_data;
			lv_port_sem_take();
			_ensure_display_modal();
			_apply_display_cfg_to_widgets(cfg);
			lv_port_sem_give();
			break;
		}
		default:
			break;
	}
}

int indicator_display_view_init(void) {
	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_START,
		_view_event_handler, NULL, NULL));

	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_DISPLAY_CFG,
		_view_event_handler, NULL, NULL));

	return 0;
}
