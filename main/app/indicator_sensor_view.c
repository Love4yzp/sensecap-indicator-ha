#include "indicator_sensor.h"

#include "ui.h"
#include "view_data.h"

// #define VIEW_DEBUG
#define BUF_SIZE 32

static const char* TAG = "sensor_view";

// typedef struct SensorView {
//     lv_obj_t* ui_lbl;
// } SensorView;

typedef struct SensorView
{
	lv_obj_t** ui_lbl;
	int ui_lbl_size;
} SensorView;

static SensorView sensorPanel[ENUM_SENSOR_ALL] = {NULL};

static void format_sensor_data(char* buf, enum sensor_data_type sensor_type, const float data);

/**
 * @brief Get the data from RP2040
 * @attention called in indicator_view.c
 */
void view_event_update_present_sensorData(void* handler_args, esp_event_base_t base, int32_t id,
										  void* event_data) {
	if(id != VIEW_EVENT_SENSOR_DATA)
		return;
	// ESP_LOGI(TAG, "event: VIEW_EVENT_SENSOR_DATA");
	struct view_data_sensor_data* p_data = (struct view_data_sensor_data*)event_data;
	if(p_data == NULL)
	{
		ESP_LOGE(TAG, "event_data is NULL");
		return;
	}
	if(sensorPanel[p_data->sensor_type].ui_lbl == NULL)
	{
		ESP_LOGE(TAG, "SensePanel did't init completely");
		return;
	}
	char data_buf[BUF_SIZE];

	memset(data_buf, 0, sizeof(data_buf));

	format_sensor_data(data_buf, p_data->sensor_type, p_data->value); // entry

#ifdef VIEW_DEBUG
	ESP_LOGI(TAG, "update %s:%s", enum sensor_data_typeStrings[p_data->sensor_type], data_buf);
#endif
	lv_port_sem_take();
	for(int i = 0; i < sensorPanel[p_data->sensor_type].ui_lbl_size; i++)
	{
		lv_label_set_text(sensorPanel[p_data->sensor_type].ui_lbl[i], data_buf); // update ui lable
	}
	// lv_label_set_text(sensorPanel[p_data->sensor_type].ui_lbl, data_buf); // update ui lable
	lv_port_sem_give();
}

// format sensor data according to sensor type
static void format_sensor_data(char* buf, enum sensor_data_type sensor_type, const float data) {
	char* format_style;

	if(data < 0)
	{
		snprintf(buf, BUF_SIZE, "N/A");
		return;
	}

	switch(sensor_type)
	{
		case SHT41_SENSOR_TEMP:
			format_style = "%.1f";
			break;
		case SCD41_SENSOR_CO2:
		case SGP40_SENSOR_TVOC:
		case SHT41_SENSOR_HUMIDITY:
		default:
			format_style = "%.0f";
			break;
	}
	snprintf(buf, BUF_SIZE, format_style, data); // wrtie to buf
}

void view_sensor_init() {
	// if(ui_screen_ha_mix == NULL || ui_screen_ha_data)
	// {
	// 	ESP_LOGE(TAG, "Sensor Screen is not init");
	// 	return;
	// }
	// Once you add or remove a sensor, you need to update the sensorPanel
	// sensorPanel[SCD41_SENSOR_CO2].ui_lbl = ui_LblDataCO2;       // inner
	// sensorPanel[SGP40_SENSOR_TVOC].ui_lbl = ui_LblDataTVOC;     // inner
	// sensorPanel[SHT41_SENSOR_TEMP].ui_lbl = ui_LblDataTmp;      // outer
	// sensorPanel[SHT41_SENSOR_HUMIDITY].ui_lbl = ui_LblDataHumi; // outer
	sensorPanel[SCD41_SENSOR_CO2].ui_lbl_size = 1;
	sensorPanel[SCD41_SENSOR_CO2].ui_lbl = (lv_obj_t**)malloc(sizeof(lv_obj_t*) * 2);
	sensorPanel[SCD41_SENSOR_CO2].ui_lbl[0] = ui_sensor_data_co2_2;

	sensorPanel[SGP40_SENSOR_TVOC].ui_lbl_size = 1;
	sensorPanel[SGP40_SENSOR_TVOC].ui_lbl = (lv_obj_t*)malloc(sizeof(lv_obj_t*) * 1);
	sensorPanel[SGP40_SENSOR_TVOC].ui_lbl[0] = ui_sensor_data_tvoc_2;

	sensorPanel[SHT41_SENSOR_TEMP].ui_lbl_size = 2;
	sensorPanel[SHT41_SENSOR_TEMP].ui_lbl = (lv_obj_t*)malloc(sizeof(lv_obj_t*) * 1);
	sensorPanel[SHT41_SENSOR_TEMP].ui_lbl[0] = ui_sensor_data_temp_1;
	sensorPanel[SHT41_SENSOR_TEMP].ui_lbl[1] = ui_sensor_data_temp_2;

	sensorPanel[SHT41_SENSOR_HUMIDITY].ui_lbl_size = 2;
	sensorPanel[SHT41_SENSOR_HUMIDITY].ui_lbl = (lv_obj_t*)malloc(sizeof(lv_obj_t*) * 1);
	sensorPanel[SHT41_SENSOR_HUMIDITY].ui_lbl[0] = ui_sensor_data_humi_1;
	sensorPanel[SHT41_SENSOR_HUMIDITY].ui_lbl[1] = ui_sensor_data_humi_1;

	ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
		view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SENSOR_DATA,
		view_event_update_present_sensorData, NULL, NULL));
}