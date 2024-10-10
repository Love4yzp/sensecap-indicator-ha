// This file was generated by SquareLine Studio
// SquareLine Studio version: SquareLine Studio 1.4.1
// LVGL version: 8.3.11
// Project name: indicator_ha

#include "../ui.h"

void ui_screen_ha_data_screen_init(void)
{
    ui_screen_ha_data = lv_obj_create(NULL);
    lv_obj_clear_flag(ui_screen_ha_data, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    ui_panel_top_1 = lv_obj_create(ui_screen_ha_data);
    lv_obj_set_width(ui_panel_top_1, 480);
    lv_obj_set_height(ui_panel_top_1, 100);
    lv_obj_set_align(ui_panel_top_1, LV_ALIGN_TOP_MID);
    lv_obj_clear_flag(ui_panel_top_1, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_panel_top_1, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_panel_top_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ui_panel_top_1, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(ui_panel_top_1, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_time_ha_data = lv_label_create(ui_panel_top_1);
    lv_obj_set_width(ui_time_ha_data, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_time_ha_data, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_time_ha_data, 15);
    lv_obj_set_y(ui_time_ha_data, 0);
    lv_obj_set_align(ui_time_ha_data, LV_ALIGN_LEFT_MID);
    lv_label_set_text(ui_time_ha_data, "21:20");
    lv_obj_add_flag(ui_time_ha_data, LV_OBJ_FLAG_HIDDEN);     /// Flags
    lv_obj_set_style_text_font(ui_time_ha_data, &ui_font_font0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_screen_home_data_label1 = lv_label_create(ui_panel_top_1);
    lv_obj_set_width(ui_screen_home_data_label1, 316);
    lv_obj_set_height(ui_screen_home_data_label1, 21);
    lv_obj_set_align(ui_screen_home_data_label1, LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(ui_screen_home_data_label1, "Home Assistant Data");
    lv_obj_set_style_text_color(ui_screen_home_data_label1, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_screen_home_data_label1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_screen_home_data_label1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_screen_home_data_label1, &ui_font_font0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_wifi__st_button_ha_data = lv_btn_create(ui_panel_top_1);
    lv_obj_set_width(ui_wifi__st_button_ha_data, 60);
    lv_obj_set_height(ui_wifi__st_button_ha_data, 60);
    lv_obj_set_align(ui_wifi__st_button_ha_data, LV_ALIGN_RIGHT_MID);
    lv_obj_add_flag(ui_wifi__st_button_ha_data, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(ui_wifi__st_button_ha_data, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_bg_color(ui_wifi__st_button_ha_data, lv_color_hex(0x101418), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_wifi__st_button_ha_data, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_grad_color(ui_wifi__st_button_ha_data, lv_color_hex(0x101418), LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_wifi_st_2 = lv_img_create(ui_wifi__st_button_ha_data);
    lv_img_set_src(ui_wifi_st_2, &ui_img_wifi_disconet_png);
    lv_obj_set_width(ui_wifi_st_2, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_wifi_st_2, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_align(ui_wifi_st_2, LV_ALIGN_CENTER);
    lv_obj_add_flag(ui_wifi_st_2, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_clear_flag(ui_wifi_st_2, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    ui_sensor1_btn1 = lv_btn_create(ui_screen_ha_data);
    lv_obj_set_width(ui_sensor1_btn1, 214);
    lv_obj_set_height(ui_sensor1_btn1, 164);
    lv_obj_set_x(ui_sensor1_btn1, 22);
    lv_obj_set_y(ui_sensor1_btn1, 96);
    lv_obj_add_flag(ui_sensor1_btn1, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(ui_sensor1_btn1, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_sensor1_btn1, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_sensor1_btn1, lv_color_hex(0x282828), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_sensor1_btn1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_sensor1_logo1 = lv_img_create(ui_sensor1_btn1);
    lv_img_set_src(ui_sensor1_logo1, &ui_img_ic_temp_png);
    lv_obj_set_width(ui_sensor1_logo1, LV_SIZE_CONTENT);   /// 45
    lv_obj_set_height(ui_sensor1_logo1, LV_SIZE_CONTENT);    /// 45
    lv_obj_set_x(ui_sensor1_logo1, 69);
    lv_obj_set_y(ui_sensor1_logo1, 22);
    lv_obj_add_flag(ui_sensor1_logo1, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_clear_flag(ui_sensor1_logo1, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    ui_sensor1_label1 = lv_label_create(ui_sensor1_btn1);
    lv_obj_set_width(ui_sensor1_label1, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_sensor1_label1, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_sensor1_label1, 0);
    lv_obj_set_y(ui_sensor1_label1, -5);
    lv_obj_set_align(ui_sensor1_label1, LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(ui_sensor1_label1, "Temp");
    lv_obj_set_style_text_color(ui_sensor1_label1, lv_color_hex(0x9E9E9E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_sensor1_label1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_sensor1_label1, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_sensor_data_temp_2 = lv_label_create(ui_sensor1_btn1);
    lv_obj_set_width(ui_sensor_data_temp_2, 100);
    lv_obj_set_height(ui_sensor_data_temp_2, LV_SIZE_CONTENT);    /// 2
    lv_obj_set_x(ui_sensor_data_temp_2, 11);
    lv_obj_set_y(ui_sensor_data_temp_2, 79);
    lv_label_set_text(ui_sensor_data_temp_2, "N/A");
    lv_obj_set_style_text_color(ui_sensor_data_temp_2, lv_color_hex(0xECBF41), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_sensor_data_temp_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_sensor_data_temp_2, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_sensor_data_temp_2, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_sensor1_unit1 = lv_label_create(ui_sensor1_btn1);
    lv_obj_set_width(ui_sensor1_unit1, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_sensor1_unit1, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_sensor1_unit1, 125);
    lv_obj_set_y(ui_sensor1_unit1, 82);
    lv_label_set_text(ui_sensor1_unit1, "°C");
    lv_obj_set_style_text_color(ui_sensor1_unit1, lv_color_hex(0xECBF41), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_sensor1_unit1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_sensor1_unit1, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_sensor2_btn1 = lv_btn_create(ui_screen_ha_data);
    lv_obj_set_width(ui_sensor2_btn1, 214);
    lv_obj_set_height(ui_sensor2_btn1, 164);
    lv_obj_set_x(ui_sensor2_btn1, 244);
    lv_obj_set_y(ui_sensor2_btn1, 96);
    lv_obj_add_flag(ui_sensor2_btn1, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(ui_sensor2_btn1, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_sensor2_btn1, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_sensor2_btn1, lv_color_hex(0x282828), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_sensor2_btn1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_sensor2_logo1 = lv_img_create(ui_sensor2_btn1);
    lv_img_set_src(ui_sensor2_logo1, &ui_img_ic_hum_png);
    lv_obj_set_width(ui_sensor2_logo1, LV_SIZE_CONTENT);   /// 45
    lv_obj_set_height(ui_sensor2_logo1, LV_SIZE_CONTENT);    /// 45
    lv_obj_set_x(ui_sensor2_logo1, 69);
    lv_obj_set_y(ui_sensor2_logo1, 22);
    lv_obj_add_flag(ui_sensor2_logo1, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_clear_flag(ui_sensor2_logo1, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    ui_sensor2_label1 = lv_label_create(ui_sensor2_btn1);
    lv_obj_set_width(ui_sensor2_label1, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_sensor2_label1, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_sensor2_label1, 0);
    lv_obj_set_y(ui_sensor2_label1, -5);
    lv_obj_set_align(ui_sensor2_label1, LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(ui_sensor2_label1, "Humidity");
    lv_obj_set_style_text_color(ui_sensor2_label1, lv_color_hex(0x9E9E9E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_sensor2_label1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_sensor2_label1, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_sensor_data_humi_2 = lv_label_create(ui_sensor2_btn1);
    lv_obj_set_width(ui_sensor_data_humi_2, 100);
    lv_obj_set_height(ui_sensor_data_humi_2, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_sensor_data_humi_2, 16);
    lv_obj_set_y(ui_sensor_data_humi_2, 83);
    lv_label_set_text(ui_sensor_data_humi_2, "N/A");
    lv_obj_set_style_text_color(ui_sensor_data_humi_2, lv_color_hex(0x52AAE5), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_sensor_data_humi_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_sensor_data_humi_2, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_sensor_data_humi_2, &ui_font_font2, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_sensor2_unit1 = lv_label_create(ui_sensor2_btn1);
    lv_obj_set_width(ui_sensor2_unit1, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_sensor2_unit1, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_sensor2_unit1, 125);
    lv_obj_set_y(ui_sensor2_unit1, 82);
    lv_label_set_text(ui_sensor2_unit1, "%");
    lv_obj_set_style_text_color(ui_sensor2_unit1, lv_color_hex(0x52AAE5), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_sensor2_unit1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_sensor2_unit1, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_sensor3_btn1 = lv_btn_create(ui_screen_ha_data);
    lv_obj_set_width(ui_sensor3_btn1, 214);
    lv_obj_set_height(ui_sensor3_btn1, 164);
    lv_obj_set_x(ui_sensor3_btn1, 22);
    lv_obj_set_y(ui_sensor3_btn1, 268);
    lv_obj_add_flag(ui_sensor3_btn1, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(ui_sensor3_btn1, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_sensor3_btn1, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_sensor3_btn1, lv_color_hex(0x282828), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_sensor3_btn1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_sensor3_logo1 = lv_img_create(ui_sensor3_btn1);
    lv_img_set_src(ui_sensor3_logo1, &ui_img_ic_tvoc_png);
    lv_obj_set_width(ui_sensor3_logo1, LV_SIZE_CONTENT);   /// 45
    lv_obj_set_height(ui_sensor3_logo1, LV_SIZE_CONTENT);    /// 45
    lv_obj_set_x(ui_sensor3_logo1, 69);
    lv_obj_set_y(ui_sensor3_logo1, 22);
    lv_obj_add_flag(ui_sensor3_logo1, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_clear_flag(ui_sensor3_logo1, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    ui_sensor3_label1 = lv_label_create(ui_sensor3_btn1);
    lv_obj_set_width(ui_sensor3_label1, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_sensor3_label1, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_sensor3_label1, 0);
    lv_obj_set_y(ui_sensor3_label1, -5);
    lv_obj_set_align(ui_sensor3_label1, LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(ui_sensor3_label1, "tVOC");
    lv_obj_set_style_text_color(ui_sensor3_label1, lv_color_hex(0x9E9E9E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_sensor3_label1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_sensor3_label1, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_sensor_data_tvoc_2 = lv_label_create(ui_sensor3_btn1);
    lv_obj_set_width(ui_sensor_data_tvoc_2, 100);
    lv_obj_set_height(ui_sensor_data_tvoc_2, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_sensor_data_tvoc_2, 11);
    lv_obj_set_y(ui_sensor_data_tvoc_2, 83);
    lv_label_set_text(ui_sensor_data_tvoc_2, "N/A");
    lv_obj_set_style_text_color(ui_sensor_data_tvoc_2, lv_color_hex(0xD76D46), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_sensor_data_tvoc_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_sensor_data_tvoc_2, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_sensor_data_tvoc_2, &ui_font_font2, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_sensor3_unit1 = lv_label_create(ui_sensor3_btn1);
    lv_obj_set_width(ui_sensor3_unit1, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_sensor3_unit1, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_sensor3_unit1, 117);
    lv_obj_set_y(ui_sensor3_unit1, 82);
    lv_label_set_text(ui_sensor3_unit1, "index");
    lv_label_set_recolor(ui_sensor3_unit1, "true");
    lv_obj_set_style_text_color(ui_sensor3_unit1, lv_color_hex(0xD76D46), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_sensor3_unit1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_sensor3_unit1, &lv_font_montserrat_22, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_sensor4_btn1 = lv_btn_create(ui_screen_ha_data);
    lv_obj_set_width(ui_sensor4_btn1, 214);
    lv_obj_set_height(ui_sensor4_btn1, 164);
    lv_obj_set_x(ui_sensor4_btn1, 244);
    lv_obj_set_y(ui_sensor4_btn1, 268);
    lv_obj_add_flag(ui_sensor4_btn1, LV_OBJ_FLAG_SCROLL_ON_FOCUS);     /// Flags
    lv_obj_clear_flag(ui_sensor4_btn1, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
    lv_obj_set_style_radius(ui_sensor4_btn1, 12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui_sensor4_btn1, lv_color_hex(0x282828), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ui_sensor4_btn1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_sensor4_logo1 = lv_img_create(ui_sensor4_btn1);
    lv_img_set_src(ui_sensor4_logo1, &ui_img_ic_co2_png);
    lv_obj_set_width(ui_sensor4_logo1, LV_SIZE_CONTENT);   /// 45
    lv_obj_set_height(ui_sensor4_logo1, LV_SIZE_CONTENT);    /// 45
    lv_obj_set_x(ui_sensor4_logo1, 69);
    lv_obj_set_y(ui_sensor4_logo1, 22);
    lv_obj_add_flag(ui_sensor4_logo1, LV_OBJ_FLAG_ADV_HITTEST);     /// Flags
    lv_obj_clear_flag(ui_sensor4_logo1, LV_OBJ_FLAG_SCROLLABLE);      /// Flags

    ui_sensor4_label1 = lv_label_create(ui_sensor4_btn1);
    lv_obj_set_width(ui_sensor4_label1, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_sensor4_label1, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_sensor4_label1, 0);
    lv_obj_set_y(ui_sensor4_label1, -5);
    lv_obj_set_align(ui_sensor4_label1, LV_ALIGN_BOTTOM_MID);
    lv_label_set_text(ui_sensor4_label1, "CO2");
    lv_obj_set_style_text_color(ui_sensor4_label1, lv_color_hex(0x9E9E9E), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_sensor4_label1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_sensor4_label1, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_sensor_data_co2_2 = lv_label_create(ui_sensor4_btn1);
    lv_obj_set_width(ui_sensor_data_co2_2, 100);
    lv_obj_set_height(ui_sensor_data_co2_2, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_sensor_data_co2_2, 13);
    lv_obj_set_y(ui_sensor_data_co2_2, 84);
    lv_label_set_text(ui_sensor_data_co2_2, "N/A");
    lv_obj_set_style_text_color(ui_sensor_data_co2_2, lv_color_hex(0x4F9E52), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_sensor_data_co2_2, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(ui_sensor_data_co2_2, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_sensor_data_co2_2, &ui_font_font2, LV_PART_MAIN | LV_STATE_DEFAULT);

    ui_sensor4_unit1 = lv_label_create(ui_sensor4_btn1);
    lv_obj_set_width(ui_sensor4_unit1, LV_SIZE_CONTENT);   /// 1
    lv_obj_set_height(ui_sensor4_unit1, LV_SIZE_CONTENT);    /// 1
    lv_obj_set_x(ui_sensor4_unit1, 119);
    lv_obj_set_y(ui_sensor4_unit1, 80);
    lv_label_set_text(ui_sensor4_unit1, "ppm");
    lv_obj_set_style_text_color(ui_sensor4_unit1, lv_color_hex(0x4F9E52), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_opa(ui_sensor4_unit1, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ui_sensor4_unit1, &lv_font_montserrat_24, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_add_event_cb(ui_wifi__st_button_ha_data, ui_event_wifi__st_button_ha_data, LV_EVENT_ALL, NULL);
    lv_obj_add_event_cb(ui_screen_ha_data, ui_event_screen_ha_data, LV_EVENT_ALL, NULL);

}
