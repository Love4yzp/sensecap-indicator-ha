
#include "core/lv_obj.h"
#include "indicator_ha.h"
#include "misc/lv_anim.h"
#include "view_data.h"
#include "ui.h"

#define HA_CFG_STORAGE "ha-cfg"

static const char* TAG = "ha-view";

static void __view_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data) {
	lv_port_sem_take();
	switch(id)
	{
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
}