
#include "core/lv_obj.h"
#include "extra/widgets/msgbox/lv_msgbox.h"
#include "indicator_ha.h"
#include "misc/lv_anim.h"
#include "view_data.h"
#include "ui.h"
#include "widgets/lv_textarea.h"
#include <regex.h>
#include <stdbool.h>

#define HA_CFG_STORAGE	   "ha-cfg"
#define MAX_BROKER_URL_LEN 128
#define MAX_CLIENT_ID_LEN  16
#define MAX_USERNAME_LEN   32
#define MAX_PASSWORD_LEN   64

static const char* TAG = "ha-view";

static ha_cfg_interface ha_cfg;

static bool is_valid_ipv4(const char* ip_address) {
	regex_t regex;
	int reti;
	bool is_valid = false;

	// IPv4 address pattern
	// Matches numbers 0-255 for each octet
	const char* pattern =
		"^([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.([0-9]|"
		"[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$";

	// Compile regular expression
	reti = regcomp(&regex, pattern, REG_EXTENDED);
	if(reti)
	{
		ESP_LOGE(TAG, "Could not compile regex");
		return false;
	}

	// Execute regular expression
	reti = regexec(&regex, ip_address, 0, NULL, 0);
	if(!reti)
	{
		is_valid = true;
	}

	// Free compiled regular expression
	regfree(&regex);

	return is_valid;
}
static bool extract_ip_from_url(const char *url, char *ip, size_t ip_size) {
    regex_t regex;
    regmatch_t matches[2];
    const char *pattern = "([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)";
    
    if (regcomp(&regex, pattern, REG_EXTENDED) != 0) {
        ESP_LOGE(TAG, "Failed to compile regex");
        return false;
    }
    
    if (regexec(&regex, url, 2, matches, 0) == 0) {
        size_t len = matches[1].rm_eo - matches[1].rm_so;
        if (len < ip_size) {
            strncpy(ip, url + matches[1].rm_so, len);
            ip[len] = '\0';
            regfree(&regex);
            return true;
        }
    }
    
    regfree(&regex);
    return false;
}

static void update_ip_textfield(const char *broker_url) {
    char ip[16];
    if (extract_ip_from_url(broker_url, ip, sizeof(ip))) {
        lv_textarea_set_text(ui_textarea_ip_0, ip);
    } else {
        ESP_LOGE(TAG, "Failed to extract IP from URL: %s", broker_url);
        lv_textarea_set_text(ui_textarea_ip_0, "");
    }
}

void assemble_broker_url(const char* ip_address, char* broker_url, size_t broker_url_size) {
	const char* prefix = "mqtt://"; // MQTT Protocol prefix
	// const char* suffix = ":1883"; // MQTT The default port
	const char* suffix = ""; // The default port

	// 组装成完整的 broker URL，确保总长度不超过目标数组的大小
	snprintf(broker_url, broker_url_size, "%s%s%s", prefix, ip_address, suffix);
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

static void __view_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data) {
	lv_port_sem_take();

	switch(id)
	{
		case VIEW_EVENT_MQTT_ADDR_CHANGED:
		{
			const char* new_broker_ip = lv_textarea_get_text(ui_textarea_ip_0);

			if(is_valid_ipv4(new_broker_ip))
			{
				ha_cfg_get(&ha_cfg); // Get current config

				size_t url_len = strlen(new_broker_ip);
				size_t total_url_len = url_len + 13; //  "mqtt://ip:1883" A total of 13 characters

				// Construct the full broker URL with default MQTT port
				char broker_url[MAX_BROKER_URL_LEN];
				assemble_broker_url(new_broker_ip, broker_url, sizeof(broker_url));

				if(total_url_len < sizeof(ha_cfg.broker_url))
				{
					strlcpy(ha_cfg.broker_url, broker_url, sizeof(ha_cfg.broker_url));

					if(ha_cfg_set(&ha_cfg) == ESP_OK)
					{
						ESP_LOGI(TAG, "Valid broker URL saved: %s", ha_cfg.broker_url);
						// TODO: Trigger MQTT reconnection
						show_message_box("Broker IP updated successfully", lv_palette_main(LV_PALETTE_GREEN));
					}
					else
					{
						ESP_LOGE(TAG, "Failed to save broker IP");
						show_message_box("Failed to save broker IP", lv_palette_main(LV_PALETTE_RED));
					}
				}
				else
				{
					ESP_LOGE(TAG, "Broker URL too long");
					show_message_box("Broker URL too long", lv_palette_main(LV_PALETTE_RED));
				}
			}
			else
			{
				ESP_LOGE(TAG, "Invalid IPv4 address: %s", new_broker_ip);
				show_message_box("Invalid IPv4 address", lv_palette_main(LV_PALETTE_RED));
			}
			break;
		}
		// case VIEW_EVENT_HA_SENSOR:
		// {
		// 	ESP_LOGI(TAG, "event: VIEW_EVENT_HA_SENSOR");
		// 	struct view_data_ha_sensor_data* p_data = (struct view_data_ha_sensor_data*)event_data;

		// 	switch(p_data->index)
		// 	{
		// 		case 0:
		// 		{
		// 			lv_label_set_text(ui_sensor_data_co2_2, p_data->value);
		// 			break;
		// 		}
		// 		case 1:
		// 		{
		// 			lv_label_set_text(ui_sensor_data_temp_1, p_data->value);
		// 			lv_label_set_text(ui_sensor_data_temp_2, p_data->value);
		// 			break;
		// 		}
		// 		case 2:
		// 		{
		// 			lv_label_set_text(ui_sensor_data_humi_1, p_data->value);
		// 			break;
		// 		}
		// 		case 3:
		// 		{
		// 			lv_label_set_text(ui_sensor_data_tvoc_2, p_data->value);
		// 			break;
		// 		}
		// 		case 4:
		// 		{
		// 			lv_label_set_text(ui_sensor5_data, p_data->value);
		// 			break;
		// 		}
		// 		case 5:
		// 		{
		// 			lv_label_set_text(ui_sensor6_data, p_data->value);
		// 			break;
		// 		}
		// 		default:
		// 			break;
		// 	}
		// 	break;
		// }
		case VIEW_EVENT_HA_SWITCH_SET: /* restore */
		{
			struct view_data_ha_switch_data* p_data = (struct view_data_ha_switch_data*)event_data;
			lv_obj_t* target;
			ESP_LOGI(TAG, "VIEW_EVENT_HA_SWITCH_SET index:%d", p_data->index);
			switch(p_data->index)
			{
				case 0:
				{
					target = ui_switch1;
					break;
				}
				case 1:
				{
					target = ui_switch2;
					break;
				}
				case 2:
				{
					target = ui_switch3;
					break;
				}
				case 3:
				{
					target = ui_switch4;
					break;
				}
				case 4:
				{
					target = ui_switch5;
					break;
				}
				case 5:
				{
					target = ui_switch6;
					break;
				}
				case 6:
				{
					target = ui_switch5_arc1;
					break;
				}
				case 7:
				{
					target = ui_switch8_slider1;
					break;
				}
			}
			if(target != ui_switch5_arc1 && target != ui_switch8_slider1)
			{
				if(p_data->value)
				{
					lv_obj_add_state(target, LV_STATE_CHECKED);
				}
				else
				{
					lv_obj_clear_state(target, LV_STATE_CHECKED);
				}
			}
			else if(target == ui_switch5_arc1)
			{
				char buf[_UI_TEMPORARY_STRING_BUFFER_SIZE];

				lv_snprintf(buf, sizeof(buf), "%s%d%s", "", p_data->value, " °C");

				lv_label_set_text(ui_switch5_arc_data1, buf);

				lv_arc_set_value(ui_switch5_arc1, p_data->value);
			}
			else if(target == ui_switch8_slider1)
			{
				lv_slider_set_value(ui_switch8_slider1, p_data->value, LV_ANIM_OFF);
			}
			else
			{
				ESP_LOGW(TAG, "Wrong processing widget");
			}
		}
		default:
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

	ha_cfg_get(&ha_cfg); // Get current config
	update_ip_textfield(ha_cfg.broker_url);
}