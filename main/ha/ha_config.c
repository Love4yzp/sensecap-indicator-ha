#include <string.h>

#include "ha_config.h"
#include "ha_mqtt.h"
#include "view_data.h"
#include "home_assistant_config.h"
#include "storage_nvs.h"
#include "lv_port.h"
#include "indicator_util.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define MAX_BROKER_URL_LEN 128

static const char *TAG = "ha-config";

static lv_obj_t *s_broker_modal = NULL;
static lv_obj_t *s_broker_ip_textarea = NULL;
static lv_obj_t *s_broker_keyboard = NULL;

static const char *_get_broker_url(const void *event_data)
{
    if (event_data) {
        return (const char *)event_data;
    }

    static ha_cfg_interface ha_cfg;
    if (ha_cfg_get(&ha_cfg) == ESP_OK) {
        return ha_cfg.broker_url;
    }

    return NULL;
}

static void btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        lv_obj_t *mbox = (lv_obj_t *)lv_event_get_user_data(e);
        lv_msgbox_close(mbox);
    }
}

static void show_message_box(const char *message, lv_color_t color)
{
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Notification");
    lv_msgbox_add_text(mbox, message);
    lv_obj_t *ok_btn = lv_msgbox_add_footer_button(mbox, "OK");

    lv_obj_set_style_bg_color(mbox, color, LV_PART_MAIN);
    lv_obj_set_style_text_color(mbox, lv_color_white(), LV_PART_MAIN);
    lv_obj_add_event_cb(ok_btn, btn_event_cb, LV_EVENT_CLICKED, mbox);
    lv_obj_center(mbox);
}

static void _hide_broker_modal(void)
{
    if (s_broker_keyboard) {
        lv_obj_add_flag(s_broker_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_broker_modal) {
        lv_obj_add_flag(s_broker_modal, LV_OBJ_FLAG_HIDDEN);
    }
}

static void _on_broker_back(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    _hide_broker_modal();
}

static void _on_broker_confirm(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    /* LVGL task context (confirm tap): bound the post and warn on drop. */
    esp_err_t err = esp_event_post_to(view_event_handle, VIEW_EVENT_BASE,
                                      VIEW_EVENT_MQTT_ADDR_CHANGED, NULL, 0, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "drop VIEW_EVENT_MQTT_ADDR_CHANGED: %s", esp_err_to_name(err));
    }
}

static void _on_ip_textarea_clicked(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED || !s_broker_keyboard) {
        return;
    }

    lv_obj_remove_flag(s_broker_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void _on_broker_keyboard_done(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_READY && code != LV_EVENT_CANCEL && code != LV_EVENT_DEFOCUSED) {
        return;
    }

    if (s_broker_keyboard) {
        lv_obj_add_flag(s_broker_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void _ensure_broker_modal(void)
{
    if (s_broker_modal) {
        return;
    }

    s_broker_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_broker_modal, CONFIG_LCD_EVB_SCREEN_WIDTH, CONFIG_LCD_EVB_SCREEN_HEIGHT);
    lv_obj_set_align(s_broker_modal, LV_ALIGN_CENTER);
    lv_obj_add_flag(s_broker_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_broker_modal, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(s_broker_modal, lv_color_hex(0x101418),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_broker_modal, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_broker_modal, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_broker_modal, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_broker_modal, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_broker_modal, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *header = lv_obj_create(s_broker_modal);
    lv_obj_set_size(header, 480, 85);
    lv_obj_set_align(header, LV_ALIGN_TOP_MID);
    lv_obj_remove_flag(header, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(header, LV_OPA_TRANSP,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(header, LV_OPA_TRANSP,
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_t *back = lv_button_create(header);
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
    lv_obj_add_event_cb(back, _on_broker_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xe7ecef),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(back_label);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "MQTT");
    lv_obj_set_style_text_color(title, lv_color_white(),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_align(title, LV_ALIGN_BOTTOM_MID);

    lv_obj_t *container = lv_obj_create(s_broker_modal);
    lv_obj_set_size(container, 420, 160);
    lv_obj_set_align(container, LV_ALIGN_TOP_MID);
    lv_obj_set_y(container, 120);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *input_title = lv_label_create(container);
    lv_label_set_text(input_title, "MQTT Broker IP");
    lv_obj_set_align(input_title, LV_ALIGN_TOP_MID);
    lv_obj_set_y(input_title, 10);

    lv_obj_t *prefix = lv_label_create(container);
    lv_label_set_text(prefix, "mqtt://");
    lv_obj_set_align(prefix, LV_ALIGN_LEFT_MID);
    lv_obj_set_x(prefix, 25);

    s_broker_ip_textarea = lv_textarea_create(container);
    lv_obj_set_size(s_broker_ip_textarea, 155, LV_SIZE_CONTENT);
    lv_obj_set_align(s_broker_ip_textarea, LV_ALIGN_CENTER);
    lv_obj_set_x(s_broker_ip_textarea, -25);
    lv_textarea_set_accepted_chars(s_broker_ip_textarea, "0123456789.");
    lv_textarea_set_max_length(s_broker_ip_textarea, 20);
    lv_textarea_set_placeholder_text(s_broker_ip_textarea, "192.168.1.10");
    lv_textarea_set_one_line(s_broker_ip_textarea, true);
    lv_obj_add_event_cb(s_broker_ip_textarea, _on_ip_textarea_clicked,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_broker_ip_textarea, _on_broker_keyboard_done,
                        LV_EVENT_DEFOCUSED, NULL);

    lv_obj_t *suffix = lv_label_create(container);
    lv_label_set_text(suffix, ":1883");
    lv_obj_set_align(suffix, LV_ALIGN_CENTER);
    lv_obj_set_x(suffix, 85);

    lv_obj_t *confirm = lv_button_create(container);
    lv_obj_set_size(confirm, 94, 50);
    lv_obj_set_align(confirm, LV_ALIGN_RIGHT_MID);
    lv_obj_set_style_bg_color(confirm, lv_color_hex(0x4AAEE6),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(confirm, _on_broker_confirm, LV_EVENT_CLICKED, NULL);
    lv_obj_t *confirm_label = lv_label_create(confirm);
    lv_label_set_text(confirm_label, "Confirm");
    lv_obj_center(confirm_label);

    s_broker_keyboard = lv_keyboard_create(s_broker_modal);
    lv_keyboard_set_mode(s_broker_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_keyboard_set_textarea(s_broker_keyboard, s_broker_ip_textarea);
    lv_obj_set_size(s_broker_keyboard, 480, 240);
    lv_obj_set_align(s_broker_keyboard, LV_ALIGN_BOTTOM_MID);
    lv_obj_add_flag(s_broker_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_broker_keyboard, _on_broker_keyboard_done,
                        LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(s_broker_keyboard, _on_broker_keyboard_done,
                        LV_EVENT_CANCEL, NULL);
}

static void update_ip_textfield(const char *broker_url)
{
    _ensure_broker_modal();
    if (!s_broker_ip_textarea) {
        return;
    }

    char ip[16];
    if (extract_ip_from_url(broker_url, ip, sizeof(ip))) {
        lv_textarea_set_text(s_broker_ip_textarea, ip);
    } else {
        ESP_LOGE(TAG, "Failed to extract IP from URL: %s", broker_url);
        lv_textarea_set_text(s_broker_ip_textarea, "");
    }
}

static void _show_broker_modal(void)
{
    _ensure_broker_modal();
    const char *broker_url = _get_broker_url(NULL);
    if (broker_url) {
        update_ip_textfield(broker_url);
    }
    if (s_broker_modal) {
        lv_obj_remove_flag(s_broker_modal, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_broker_modal);
    }
}

static void handle_mqtt_addr_change(const char *new_broker_ip)
{
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
    show_message_box("Broker IP updated successfully", lv_palette_main(LV_PALETTE_GREEN));
}

static void view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    lv_port_sem_take();

    switch (id) {
        case VIEW_EVENT_SCREEN_START: {
            if (!event_data) {
                break;
            }
            uint8_t screen = *(uint8_t *)event_data;
            if (screen == SCREEN_BROKER_MODAL) {
                _show_broker_modal();
            }
            break;
        }
        case VIEW_EVENT_MQTT_ADDR_CHANGED: {
            _ensure_broker_modal();
            const char *new_broker_ip = s_broker_ip_textarea ?
                lv_textarea_get_text(s_broker_ip_textarea) : "";
            handle_mqtt_addr_change(new_broker_ip);
            break;
        }
        case VIEW_EVENT_HA_ADDR_DISPLAY: {
            const char *broker_url = _get_broker_url(event_data);
            if (broker_url) {
                update_ip_textfield(broker_url);
            } else {
                ESP_LOGE(TAG, "Failed to get broker URL for display");
            }
            break;
        }
        default:
            ESP_LOGW(TAG, "Unhandled event: %ld", id);
            break;
    }

    lv_port_sem_give();
}

esp_err_t ha_cfg_get(ha_cfg_interface *ha_cfg)
{
    int len = sizeof(ha_cfg_interface);
    memset(ha_cfg, 0, sizeof(ha_cfg_interface));
    esp_err_t err = indicator_nvs_read(MQTT_HA_CFG_STORAGE, ha_cfg, &len);
    if (err == ESP_OK && len == sizeof(ha_cfg_interface)) {
        ESP_LOGI(TAG, "mqtt broker cfg read successful");
    } else {
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "mqtt broker cfg not find");
        } else {
            ESP_LOGI(TAG, "mqtt broker cfg read err:%d", err);
        }
        strlcpy(ha_cfg->broker_url, CONFIG_BROKER_URL, sizeof(ha_cfg->broker_url));
        strlcpy(ha_cfg->client_id, CONFIG_MQTT_CLIENT_ID, sizeof(ha_cfg->client_id));
        strlcpy(ha_cfg->username, CONFIG_MQTT_USERNAME, sizeof(ha_cfg->username));
        strlcpy(ha_cfg->password, CONFIG_MQTT_PASSWORD, sizeof(ha_cfg->password));
    }
    return err;
}

esp_err_t ha_cfg_set(ha_cfg_interface *cfg)
{
    esp_err_t err = indicator_nvs_write(MQTT_HA_CFG_STORAGE, cfg, sizeof(ha_cfg_interface));
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "ha cfg write err:%d", err);
    } else {
        ESP_LOGI(TAG, "ha cfg write successful");
    }
    return err;
}

void ha_config_view_init(void)
{
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_START, view_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_MQTT_ADDR_CHANGED, view_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_ADDR_DISPLAY, view_event_handler, NULL, NULL));
}
