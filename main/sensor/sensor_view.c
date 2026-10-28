#include <stdio.h>
#include "esp_err.h"
#include "esp_log.h"
#include <string.h>

#include "sensor_model.h"
#include "sensor_view.h"

#include "home_assistant_config.h"
#include "lv_port.h"
#include "view_data.h"
#include "view_event.h"

// #define VIEW_DEBUG
#define BUF_SIZE 32

#define SENSOR_CARD_WIDTH  214
#define SENSOR_CARD_HEIGHT 164

LV_IMAGE_DECLARE(ui_img_ic_temp_png);
LV_IMAGE_DECLARE(ui_img_ic_hum_png);
LV_IMAGE_DECLARE(ui_img_ic_tvoc_png);
LV_IMAGE_DECLARE(ui_img_ic_co2_png);
LV_FONT_DECLARE(ui_font_font0);

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

typedef struct SensorCardSpec
{
	enum sensor_data_type type;
	const lv_image_dsc_t* icon;
	const char* name;
	const char* unit;
	uint32_t accent_color;
	int32_t x;
	int32_t y;
	lv_obj_t* labels[1];
} SensorCardSpec;

static SensorCardSpec sensor_card_specs[] = {
	{
		.type = SHT41_SENSOR_TEMP,
		.icon = &ui_img_ic_temp_png,
		.name = "Temp",
		.unit = CONFIG_SENSOR1_UI_UNIT,
		.accent_color = 0xECBF41,
		.x = 22,
		.y = 96,
	},
	{
		.type = SHT41_SENSOR_HUMIDITY,
		.icon = &ui_img_ic_hum_png,
		.name = "Humidity",
		.unit = "%",
		.accent_color = 0x52AAE5,
		.x = 244,
		.y = 96,
	},
	{
		.type = SGP40_SENSOR_TVOC,
		.icon = &ui_img_ic_tvoc_png,
		.name = "tVOC",
		.unit = "index",
		.accent_color = 0xD76D46,
		.x = 22,
		.y = 268,
	},
	{
		.type = SCD41_SENSOR_CO2,
		.icon = &ui_img_ic_co2_png,
		.name = "CO2",
		.unit = "ppm",
		.accent_color = 0x4F9E52,
		.x = 244,
		.y = 268,
	},
};

static void sensor_view_create_header(lv_obj_t* tile) {
	lv_obj_t* header = lv_obj_create(tile);
	lv_obj_set_size(header, 480, 85);
	lv_obj_set_pos(header, 0, 0);
	lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_width(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_pad_all(header, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

	lv_obj_t* title = lv_label_create(header);
	lv_label_set_text(title, "Home Assistant Data");
	lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_opa(title, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(title, &ui_font_font0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_center(title);
}

static void sensor_view_create_card(lv_obj_t* tile, SensorCardSpec* spec) {
	lv_color_t accent = lv_color_hex(spec->accent_color);

	lv_obj_t* card = lv_obj_create(tile);
	lv_obj_set_size(card, SENSOR_CARD_WIDTH, SENSOR_CARD_HEIGHT);
	lv_obj_set_pos(card, spec->x, spec->y);
	lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_radius(card, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_color(card, lv_color_hex(0x282828), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_border_width(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

	lv_obj_t* icon = lv_image_create(card);
	lv_image_set_src(icon, spec->icon);
	lv_obj_set_pos(icon, 69, 22);
	lv_obj_remove_flag(icon, LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t* data = lv_label_create(card);
	lv_obj_set_width(data, 100);
	lv_obj_set_height(data, LV_SIZE_CONTENT);
	lv_obj_set_pos(data, 11, 79);
	lv_label_set_text(data, "N/A");
	lv_obj_set_style_text_color(data, accent, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_opa(data, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_align(data, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(data, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);

	lv_obj_t* unit = lv_label_create(card);
	lv_label_set_text(unit, spec->unit);
	lv_obj_set_style_text_color(unit, accent, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_opa(unit, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(unit, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_align(unit, LV_ALIGN_BOTTOM_RIGHT, -18, -53);

	lv_obj_t* name = lv_label_create(card);
	lv_label_set_text(name, spec->name);
	lv_obj_set_align(name, LV_ALIGN_BOTTOM_MID);
	lv_obj_set_y(name, -5);
	lv_obj_set_style_text_color(name, lv_color_hex(0x9E9E9E), LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_opa(name, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
	lv_obj_set_style_text_font(name, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

	spec->labels[0] = data;
	sensorPanel[spec->type].ui_lbl_size = 1;
	sensorPanel[spec->type].ui_lbl = spec->labels;
}

/**
 * @brief Get the data from RP2040
 * @attention called in indicator_view.c
 */
void view_event_update_present_sensorData(
	void* handler_args, enum view_event_id id, const void* event_data) {
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
	lv_port_sem_take();
	lv_obj_t* tile = nav_get_tile(NAV_TILE_HA_DATA);
	if(tile == NULL)
	{
		lv_port_sem_give();
		ESP_LOGE(TAG, "Sensor data tile is not init");
		return;
	}

	sensor_view_create_header(tile);
	for(size_t i = 0; i < sizeof(sensor_card_specs) / sizeof(sensor_card_specs[0]); i++)
	{
		sensor_view_create_card(tile, &sensor_card_specs[i]);
	}
	lv_port_sem_give();

	ESP_ERROR_CHECK(view_event_subscribe(
		VIEW_EVENT_SENSOR_DATA,
		view_event_update_present_sensorData,
		NULL) ? ESP_OK : ESP_FAIL);
}
