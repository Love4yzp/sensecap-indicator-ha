#include "core/lv_obj.h"
#include "extra/widgets/msgbox/lv_msgbox.h"
#include "indicator_ha.h"
#include "misc/lv_anim.h"
#include "view_data.h"
#include "ui.h"
#include "widgets/lv_textarea.h"
#include "indicator_util.h"
#include <stdbool.h>

#define HA_CFG_STORAGE	   "ha-cfg"
#define MAX_BROKER_URL_LEN 128
#define MAX_CLIENT_ID_LEN  16
#define MAX_USERNAME_LEN   32
#define MAX_PASSWORD_LEN   64

static const char* TAG = "ha-view";

static ha_cfg_interface ha_cfg;

static const char* _get_broker_url(const void* event_data) {
    if (event_data) {
        return (const char*)event_data;
    }

    static ha_cfg_interface ha_cfg;
    if (ha_cfg_get(&ha_cfg) == ESP_OK) {
        return ha_cfg.broker_url;
    }

    return NULL;
}

static void btn_event_cb(lv_event_t* e) {
	lv_obj_t* obj = lv_event_get_current_target(e);
	LV_LOG_USER("Button %s clicked", lv_msgbox_get_active_btn_text(obj));
	if(lv_msgbox_get_active_btn_text(obj) == "OK")
	{
		lv_msgbox_close(obj);
	}
}

static void show_message_box(const char* message, lv_color_t color) {
	static const char* btns[] = {"OK", ""};
	lv_obj_t* mbox;

	mbox = lv_msgbox_create(NULL, "Notification", message, btns, true);

	lv_obj_set_style_bg_color(mbox, color, LV_PART_MAIN);
	lv_obj_set_style_text_color(mbox, lv_color_white(), LV_PART_MAIN);
	lv_obj_add_event_cb(mbox, btn_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
	lv_obj_center(mbox);
}

static void update_ip_textfield(const char* broker_url) {
	char ip[16];
	if(extract_ip_from_url(broker_url, ip, sizeof(ip)))
	{
		lv_textarea_set_text(ui_textarea_ip_0, ip);
	}
	else
	{
		ESP_LOGE(TAG, "Failed to extract IP from URL: %s", broker_url);
		lv_textarea_set_text(ui_textarea_ip_0, "");
	}
}

static void handle_mqtt_addr_change(const char* new_broker_ip) {
    if (!is_valid_ipv4(new_broker_ip)) {
        ESP_LOGE(TAG, "Invalid IPv4 address: %s", new_broker_ip);
        show_message_box("Invalid IPv4 address", lv_palette_main(LV_PALETTE_RED));
        return;
    }

    ha_cfg_interface ha_cfg;
    ha_cfg_get(&ha_cfg);

    char broker_url[MAX_BROKER_URL_LEN];
    assemble_broker_url(new_broker_ip, broker_url, sizeof(broker_url));

    if (strlcpy(ha_cfg.broker_url, broker_url, sizeof(ha_cfg.broker_url)) >= sizeof(ha_cfg.broker_url)) {
        ESP_LOGE(TAG, "Broker URL too long");
        show_message_box("Broker URL too long", lv_palette_main(LV_PALETTE_RED));
        return;
    }

    if (ha_cfg_set(&ha_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save broker IP");
        show_message_box("Failed to save broker IP", lv_palette_main(LV_PALETTE_RED));
        return;
    }

    ESP_LOGI(TAG, "Valid broker URL saved: %s", ha_cfg.broker_url);
    esp_event_post_to(ha_cfg_event_handle, HA_CFG_EVENT_BASE, HA_CFG_BROKER_CHANGED, ha_cfg.broker_url, sizeof(ha_cfg.broker_url), portMAX_DELAY);
    // esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_ADDR_DISPLAY, ha_cfg.broker_url, sizeof(ha_cfg.broker_url), portMAX_DELAY);
    show_message_box("Broker IP updated successfully", lv_palette_main(LV_PALETTE_GREEN));
}

static void update_switch_ui(int index, int value) {

    lv_obj_t* target = NULL;
    switch (index) {
        case 0: target = ui_switch1; break;
        case 1: target = ui_switch2; break;
        case 2: target = ui_switch3; break;
        case 3: target = ui_switch4; break;
        case 4: target = ui_switch5_arc1; break;
        case 5: target = ui_switch6; break;
        case 6: target = ui_switch7; break;
        case 7: target = ui_switch8_slider1; break;
        default: ESP_LOGW(TAG, "Invalid switch index: %d", index); return;
    }

    if (target == NULL) {
        ESP_LOGW(TAG, "Target object is NULL for index: %d", index);
        return;
    }

	ESP_LOGI(TAG, "update_switch_ui index:%d value: %d", index, value);

	switch (index) {
		case 0:
		case 1:
		case 2:
		case 3:
		case 5:
		case 6: // ui_switch7
		{
			if (value) {
            	lv_obj_add_state(target, LV_STATE_CHECKED); // 按道理来说，会触发 ui_event_switch1
			} else {
				lv_obj_clear_state(target, LV_STATE_CHECKED);
			}
			break;
		}
		case 4: // ui_switch5_arc1
		{
			char buf[_UI_TEMPORARY_STRING_BUFFER_SIZE];
			lv_snprintf(buf, sizeof(buf), "%d °C", value);
			lv_label_set_text(ui_switch5_arc_data1, buf);
			lv_arc_set_value(target, value);
			break;
		}
		case 7: // ui_switch8_slider1
			lv_slider_set_value(target, value, LV_ANIM_ON);
			break;
	}
	lv_event_send(target, LV_EVENT_VALUE_CHANGED, NULL);
}


static void __view_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data) {
    lv_port_sem_take();

    switch (id) {
        case VIEW_EVENT_MQTT_ADDR_CHANGED: {
            const char* new_broker_ip = lv_textarea_get_text(ui_textarea_ip_0);
            handle_mqtt_addr_change(new_broker_ip);
            break;
        }
        case VIEW_EVENT_HA_ADDR_DISPLAY: {
			const char* broker_url = _get_broker_url(event_data);
			if (broker_url) {
				update_ip_textfield(broker_url);
			} else {
				ESP_LOGE(TAG, "Failed to get broker URL for display");
			}
			break;
        }
        case VIEW_EVENT_HA_SWITCH_SET: { // update ui
            struct view_data_ha_switch_data* p_data = (struct view_data_ha_switch_data*)event_data;
            ESP_LOGI(TAG, "VIEW_EVENT_HA_SWITCH_SET: switch index:%d value:%d\n", p_data->index, p_data->value);
            update_switch_ui(p_data->index, p_data->value);
            break;
        }
        default:
            ESP_LOGW(TAG, "Unhandled event: %ld", id);
            break;
    }

    lv_port_sem_give();
}

int indicator_ha_view_init(void) {
	// ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE,
	// VIEW_EVENT_HA_SENSOR,
	// 														 __view_event_handler, NULL, NULL));

	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SWITCH_SET, __view_event_handler, NULL, NULL));

	/* __app_config_handler */
	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_MQTT_ADDR_CHANGED, __view_event_handler, NULL, NULL));
	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_ADDR_DISPLAY, __view_event_handler, NULL, NULL));
}


