#include "wifi_view.h"
#include "esp_log.h"
#include "wifi_list_screen.h"
#include "wifi_connect_screen.h"
#include "view_data.h"
#include "lv_port.h"
#include "indicator_util.h"
#include "nav.h"
#include "sdkconfig.h"

LV_IMAGE_DECLARE(ui_img_wifi_1_png);
LV_IMAGE_DECLARE(ui_img_wifi_2_png);
LV_IMAGE_DECLARE(ui_img_wifi_3_png);
LV_IMAGE_DECLARE(ui_img_wifi_disconet_png);

static const char *TAG = "wifi-view";

static wifi_list_screen_t    *s_list_screen    = NULL;
static wifi_connect_screen_t *s_connect_screen = NULL;
static lv_obj_t              *s_wifi_modal     = NULL;
static lv_obj_t              *s_wifi_spinner   = NULL;
static lv_obj_t              *s_wifi_icon      = NULL;

static void _on_connected_tap(lv_event_t *e);
static void _on_unconnected_tap(lv_event_t *e);

/* ── local wifi modal ───────────────────────────────────────────────── */

static bool _wifi_modal_is_visible(void) {
    return s_wifi_modal && !lv_obj_has_flag(s_wifi_modal, LV_OBJ_FLAG_HIDDEN);
}

static bool s_discard_next_list = false;
static bool s_wifi_scan_pending = false;

static void _hide_wifi_modal(void) {
    if(s_connect_screen) {
        wifi_connect_screen_dismiss(s_connect_screen);
        s_connect_screen = NULL;
    }
    if(s_wifi_modal) {
        lv_obj_add_flag(s_wifi_modal, LV_OBJ_FLAG_HIDDEN);
        if(s_wifi_scan_pending) {
            s_discard_next_list = true;
        }
    }
}

static void _on_wifi_modal_back(lv_event_t *e) {
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _hide_wifi_modal();
}

static void _ensure_wifi_modal(void) {
    if(s_wifi_modal) return;

    s_wifi_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_wifi_modal, CONFIG_LCD_EVB_SCREEN_WIDTH, CONFIG_LCD_EVB_SCREEN_HEIGHT);
    lv_obj_set_align(s_wifi_modal, LV_ALIGN_CENTER);
    lv_obj_add_flag(s_wifi_modal, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_wifi_modal, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_set_style_bg_color(s_wifi_modal, lv_color_hex(0x101418),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_wifi_modal, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_wifi_modal, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_wifi_modal, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(s_wifi_modal, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_wifi_modal, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *header = lv_obj_create(s_wifi_modal);
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
    lv_obj_add_event_cb(back, _on_wifi_modal_back, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = lv_label_create(back);
    lv_label_set_text(back_label, LV_SYMBOL_LEFT " Back");
    lv_obj_set_style_text_color(back_label, lv_color_hex(0xe7ecef),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_center(back_label);

    lv_obj_t *title = lv_label_create(header);
    lv_label_set_text(title, "Wi-Fi");
    lv_obj_set_style_text_color(title, lv_color_white(),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_align(title, LV_ALIGN_BOTTOM_MID);

    s_wifi_spinner = lv_spinner_create(s_wifi_modal);
    lv_spinner_set_anim_params(s_wifi_spinner, 1000, 90);
    lv_obj_set_size(s_wifi_spinner, 50, 50);
    lv_obj_set_align(s_wifi_spinner, LV_ALIGN_CENTER);
    lv_obj_remove_flag(s_wifi_spinner, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_wifi_spinner, LV_OBJ_FLAG_HIDDEN);

    s_list_screen = wifi_list_screen_create(s_wifi_modal, s_wifi_spinner);
    wifi_list_screen_set_item_callbacks(s_list_screen, _on_connected_tap, _on_unconnected_tap);
}

static void _show_wifi_modal(void) {
    _ensure_wifi_modal();
    if(!s_wifi_modal) return;

    lv_obj_remove_flag(s_wifi_modal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_wifi_modal);
    wifi_list_screen_show_spinner(s_list_screen);

    s_wifi_scan_pending = true;
    /* Reachable from the LVGL task (icon tap) and the view loop (SCREEN_START);
     * bound the post so a full queue can never freeze either. */
    esp_err_t err = esp_event_post_to(view_event_handle, VIEW_EVENT_BASE,
                                      VIEW_EVENT_WIFI_LIST_REQ, NULL, 0, pdMS_TO_TICKS(100));
    if(err != ESP_OK) {
        ESP_LOGW(TAG, "drop VIEW_EVENT_WIFI_LIST_REQ: %s", esp_err_to_name(err));
    }
}

static void _on_wifi_icon_clicked(lv_event_t *e) {
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    _show_wifi_modal();
}

static void _ensure_wifi_status_icon(void) {
    if(s_wifi_icon) return;

    lv_obj_t *tile = nav_get_tile(NAV_TILE_HA_DATA);
    if(!tile) return;

    lv_obj_t *button = lv_button_create(tile);
    lv_obj_set_size(button, 60, 60);
    lv_obj_set_align(button, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_pos(button, -10, 10);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x101418),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER,
                            LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(button, _on_wifi_icon_clicked, LV_EVENT_CLICKED, NULL);

    s_wifi_icon = lv_image_create(button);
    lv_image_set_src(s_wifi_icon, &ui_img_wifi_disconet_png);
    lv_obj_center(s_wifi_icon);
    lv_obj_add_flag(s_wifi_icon, LV_OBJ_FLAG_ADV_HITTEST);
    lv_obj_remove_flag(s_wifi_icon, LV_OBJ_FLAG_SCROLLABLE);
}

/* ── list item tap callbacks (live here to access module state) ───────── */

static void _on_unconnected_tap(lv_event_t *e) {
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if(lv_indev_get_type(lv_indev_active()) != LV_INDEV_TYPE_POINTER) return;
    if(s_connect_screen != NULL) return; /* dialog already open */

    lv_obj_t *btn = lv_event_get_target_obj(e);
    const char *ssid = wifi_list_screen_get_item_ssid(s_list_screen, btn);
    bool have_password = (lv_obj_get_child_cnt(btn) > 2);
    s_connect_screen = wifi_connect_screen_show(ssid, have_password);
}

static void _on_connected_tap(lv_event_t *e) {
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if(lv_indev_get_type(lv_indev_active()) != LV_INDEV_TYPE_POINTER) return;
    if(s_connect_screen != NULL) return;

    lv_obj_t *btn = lv_event_get_target_obj(e);
    const char *ssid = wifi_list_screen_get_item_ssid(s_list_screen, btn);
    s_connect_screen = wifi_details_screen_show(ssid);
}

/* ── connection result toast ─────────────────────────────────────────── */

static lv_obj_t *s_result_toast = NULL;

static void _toast_close_cb(lv_timer_t *timer) {
    if(s_result_toast) {
        lv_obj_delete(s_result_toast);
        s_result_toast = NULL;
        lv_obj_remove_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_TRANSP, 0);
    }
}

static void _show_connect_result(struct view_data_wifi_connet_ret_msg *p_msg) {
    s_result_toast = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_result_toast, 300, 150);
    lv_obj_set_align(s_result_toast, LV_ALIGN_CENTER);
    lv_obj_remove_flag(s_result_toast, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(s_result_toast, lv_palette_main(LV_PALETTE_GREY), 0);

    lv_obj_t *msg = lv_label_create(s_result_toast);
    lv_label_set_text(msg, p_msg->msg);
    lv_obj_set_align(msg, LV_ALIGN_CENTER);

    lv_timer_t *timer = lv_timer_create(_toast_close_cb, 1500, NULL);
    lv_timer_set_repeat_count(timer, 1);
}

/* ── event handler ───────────────────────────────────────────────────── */

static void _view_event_handler(void *handler_args, esp_event_base_t base,
                                 int32_t id, void *event_data) {
    switch(id) {
        case VIEW_EVENT_SCREEN_START: {
            uint8_t screen = *(uint8_t *)event_data;
            if(screen == SCREEN_WIFI_CONFIG) {
                ESP_LOGI(TAG, "navigate to wifi screen");
                lv_port_sem_take();
                _show_wifi_modal();
                lv_port_sem_give();
            }
            break;
        }
        case VIEW_EVENT_WIFI_ST: {
            struct view_data_wifi_st *p_st = (struct view_data_wifi_st *)event_data;
            const void *src = &ui_img_wifi_disconet_png;

            if(p_st->is_connected) {
                switch(wifi_rssi_level_get(p_st->rssi)) {
                    case 1: src = &ui_img_wifi_1_png; break;
                    case 2: src = &ui_img_wifi_2_png; break;
                    case 3: src = &ui_img_wifi_3_png; break;
                    default: break;
                }
            }

            lv_port_sem_take();
            _ensure_wifi_status_icon();
            if(s_wifi_icon) {
                lv_image_set_src(s_wifi_icon, src);
            }
            lv_port_sem_give();
            break;
        }
        case VIEW_EVENT_WIFI_LIST_START:
            lv_port_sem_take();
            _ensure_wifi_modal();
            wifi_list_screen_show_spinner(s_list_screen);
            lv_port_sem_give();
            break;

        case VIEW_EVENT_WIFI_CONNECT:
            /* Model picks this up to connect; view shows spinner. */
            lv_port_sem_take();
            _ensure_wifi_modal();
            wifi_list_screen_show_spinner(s_list_screen);
            s_connect_screen = NULL; /* already dismissed by connect_screen itself */
            lv_port_sem_give();
            break;

        case VIEW_EVENT_WIFI_LIST_REQ:
            /* Show spinner while scan is underway. */
            lv_port_sem_take();
            _ensure_wifi_modal();
            wifi_list_screen_show_spinner(s_list_screen);
            lv_port_sem_give();
            break;

        case VIEW_EVENT_WIFI_LIST: {
            ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_LIST");
            s_wifi_scan_pending = false;
            if(s_discard_next_list) {
                s_discard_next_list = false;
                ESP_LOGI(TAG, "discard stale scan result (user backed out)");
                break;
            }
            struct view_data_wifi_list *p_list = (struct view_data_wifi_list *)event_data;
            lv_port_sem_take();
            _ensure_wifi_modal();
            wifi_list_screen_update(s_list_screen, p_list);
            lv_port_sem_give();
            break;
        }
        case VIEW_EVENT_WIFI_CONNECT_RET: {
            struct view_data_wifi_connet_ret_msg *p_data =
                (struct view_data_wifi_connet_ret_msg *)event_data;

            lv_port_sem_take();
            bool modal_visible = _wifi_modal_is_visible();
            lv_port_sem_give();
            if(!modal_visible) break;

            /* Refresh list then show result toast. Runs in the view loop task;
             * a blocking self-post here would deadlock the loop on a full
             * queue, so bound it and warn on drop. */
            esp_err_t err = esp_event_post_to(view_event_handle, VIEW_EVENT_BASE,
                                              VIEW_EVENT_WIFI_LIST_REQ, NULL, 0, pdMS_TO_TICKS(100));
            if(err != ESP_OK) {
                ESP_LOGW(TAG, "drop VIEW_EVENT_WIFI_LIST_REQ: %s", esp_err_to_name(err));
            }

            lv_port_sem_take();
            _show_connect_result(p_data);
            lv_port_sem_give();
            break;
        }
        default:
            break;
    }
}

/* ── init ────────────────────────────────────────────────────────────── */

int indicator_wifi_view_init(void) {
    lv_port_sem_take();
    _ensure_wifi_status_icon();
    lv_port_sem_give();

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST,
        _view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST,
        _view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SCREEN_START,
        _view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT_RET,
        _view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST_START,
        _view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST_REQ,
        _view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT,
        _view_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CFG_DELETE,
        _view_event_handler, NULL, NULL));

    return 0;
}
