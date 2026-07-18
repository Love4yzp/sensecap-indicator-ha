#include <stdlib.h>
#include <string.h>
#include "wifi_connect_screen.h"
#include "view_data.h"
#include "esp_log.h"

/* Re-declare only what this component needs from the asset set. */
LV_FONT_DECLARE(ui_font_font0);

static const char *TAG = "wifi-connect-screen";

struct wifi_connect_screen {
    lv_obj_t *container;
    lv_obj_t *kb;
    lv_obj_t *password_input;
    lv_obj_t *join_btn;
    char       ssid[32];
    bool       password_ready;
    wifi_connect_dismiss_cb_t on_dismiss;
    void      *dismiss_user_data;
};

/* ── internal helpers ─────────────────────────────────────────────────── */

static void _dismiss(wifi_connect_screen_t *s) {
    if(!s) return;

    /* Notify the owner FIRST, clearing our hook so it fires exactly once.
     * Doing this before teardown means a reentrant dismiss (e.g. triggered
     * while deleting objects) finds the owner's handle already NULL and bails,
     * so there is no double free. The callback only drops the owner's
     * reference; it must not free `s` or call back into dismiss.              */
    wifi_connect_dismiss_cb_t cb = s->on_dismiss;
    void *cb_data = s->dismiss_user_data;
    s->on_dismiss = NULL;
    if(cb) cb(cb_data);

    if(s->kb) {
        lv_obj_delete(s->kb);
        s->kb = NULL;
    }
    if(s->container) {
        lv_obj_delete(s->container);
        s->container = NULL;
    }
    lv_obj_remove_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_TRANSP, 0);
    free(s);
}

/* ── connect dialog callbacks ─────────────────────────────────────────── */

static void _on_cancel(lv_event_t *e) {
    wifi_connect_screen_t *s = (wifi_connect_screen_t *)lv_event_get_user_data(e);
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        _dismiss(s);
    }
}

static void _on_join(lv_event_t *e) {
    wifi_connect_screen_t *s = (wifi_connect_screen_t *)lv_event_get_user_data(e);
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    struct view_data_wifi_config cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.ssid, s->ssid, sizeof(cfg.ssid));

    if(s->kb != NULL && s->password_input != NULL) {
        cfg.have_password = true;
        const char *pw = lv_textarea_get_text(s->password_input);
        strncpy((char *)cfg.password, pw, sizeof(cfg.password));
    } else {
        cfg.have_password = false;
    }

    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CONNECT,
                      &cfg, sizeof(cfg), portMAX_DELAY);

    _dismiss(s);
}

static void _on_password_changed(lv_event_t *e) {
    wifi_connect_screen_t *s = (wifi_connect_screen_t *)lv_event_get_user_data(e);
    if(lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    if(!s->join_btn) return;

    const char *pw = lv_textarea_get_text(s->password_input);
    bool valid = (pw && strlen(pw) >= 8);

    if(valid && !s->password_ready) {
        s->password_ready = true;
        lv_obj_add_flag(s->join_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(lv_obj_get_child(s->join_btn, 0),
                                    lv_color_hex(0x529d53), LV_PART_MAIN | LV_STATE_DEFAULT);
    } else if(!valid && s->password_ready) {
        s->password_ready = false;
        lv_obj_remove_flag(s->join_btn, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_text_color(lv_obj_get_child(s->join_btn, 0),
                                    lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

static void _on_keyboard_ready(lv_event_t *e) {
    wifi_connect_screen_t *s = (wifi_connect_screen_t *)lv_event_get_user_data(e);
    if(s->password_ready && s->join_btn) {
        lv_obj_send_event(s->join_btn, LV_EVENT_CLICKED, NULL);
    }
}

/* ── details dialog callbacks ─────────────────────────────────────────── */

static void _on_delete(lv_event_t *e) {
    wifi_connect_screen_t *s = (wifi_connect_screen_t *)lv_event_get_user_data(e);
    if(lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_CFG_DELETE,
                      NULL, 0, portMAX_DELAY);
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_LIST_REQ,
                      NULL, 0, portMAX_DELAY);

    _dismiss(s);
}

/* ── public API ───────────────────────────────────────────────────────── */

wifi_connect_screen_t *wifi_connect_screen_show(const char *ssid, bool have_password) {
    wifi_connect_screen_t *s = calloc(1, sizeof(wifi_connect_screen_t));
    if(!s) return NULL;
    strncpy(s->ssid, ssid, sizeof(s->ssid));

    lv_obj_add_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(lv_layer_top(), lv_palette_main(LV_PALETTE_GREY), 0);

    s->container = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s->container, 420, 420);
    lv_obj_set_align(s->container, LV_ALIGN_CENTER);
    lv_obj_remove_flag(s->container, LV_OBJ_FLAG_SCROLLABLE);

    /* Cancel button */
    lv_obj_t *cancel_btn = lv_button_create(s->container);
    lv_obj_set_size(cancel_btn, 100, 50);
    lv_obj_set_align(cancel_btn, LV_ALIGN_TOP_LEFT);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x292831), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(cancel_btn, _on_cancel, LV_EVENT_CLICKED, s);
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_font(cancel_label, &ui_font_font0, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Join button */
    s->join_btn = lv_button_create(s->container);
    lv_obj_set_size(s->join_btn, 70, 50);
    lv_obj_set_align(s->join_btn, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_style_bg_color(s->join_btn, lv_color_hex(0x292831), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(s->join_btn, _on_join, LV_EVENT_CLICKED, s);
    lv_obj_t *join_label = lv_label_create(s->join_btn);
    lv_label_set_text(join_label, "Join");
    lv_obj_set_style_text_font(join_label, &ui_font_font0, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* SSID label */
    lv_obj_t *ssid_label = lv_label_create(s->container);
    lv_label_set_text(ssid_label, ssid);
    lv_obj_set_align(ssid_label, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ssid_label, 50);
    lv_obj_set_style_text_font(ssid_label, &ui_font_font0, LV_PART_MAIN | LV_STATE_DEFAULT);

    if(have_password) {
        /* Password label */
        lv_obj_t *pw_label = lv_label_create(s->container);
        lv_label_set_text(pw_label, "Input password");
        lv_obj_set_align(pw_label, LV_ALIGN_TOP_MID);
        lv_obj_set_pos(pw_label, -80, 100);

        /* Disable join until password is long enough */
        lv_obj_remove_flag(s->join_btn, LV_OBJ_FLAG_CLICKABLE);

        /* Password textarea */
        s->password_input = lv_textarea_create(s->container);
        lv_textarea_set_text(s->password_input, "");
        lv_textarea_set_one_line(s->password_input, true);
        lv_obj_set_width(s->password_input, lv_pct(80));
        lv_obj_set_align(s->password_input, LV_ALIGN_TOP_MID);
        lv_obj_set_y(s->password_input, 130);
        lv_obj_set_style_bg_color(s->password_input, lv_color_hex(0x6F6F6F),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_add_event_cb(s->password_input, _on_password_changed, LV_EVENT_VALUE_CHANGED, s);

        /* Keyboard */
        s->kb = lv_keyboard_create(lv_layer_top());
        lv_keyboard_set_textarea(s->kb, s->password_input);
        lv_keyboard_set_popovers(s->kb, true);
        lv_obj_add_event_cb(s->kb, _on_keyboard_ready, LV_EVENT_READY, s);
    } else {
        lv_obj_set_style_text_color(join_label, lv_color_hex(0x529d53),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    ESP_LOGI(TAG, "connect dialog open: %s", ssid);
    return s;
}

wifi_connect_screen_t *wifi_details_screen_show(const char *ssid) {
    wifi_connect_screen_t *s = calloc(1, sizeof(wifi_connect_screen_t));
    if(!s) return NULL;
    strncpy(s->ssid, ssid, sizeof(s->ssid));

    lv_obj_add_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(lv_layer_top(), lv_palette_main(LV_PALETTE_GREY), 0);

    s->container = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s->container, 300, 200);
    lv_obj_set_align(s->container, LV_ALIGN_CENTER);
    lv_obj_remove_flag(s->container, LV_OBJ_FLAG_SCROLLABLE);

    /* Cancel */
    lv_obj_t *cancel_btn = lv_button_create(s->container);
    lv_obj_set_size(cancel_btn, 100, 50);
    lv_obj_set_align(cancel_btn, LV_ALIGN_BOTTOM_RIGHT);
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0x292831), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(cancel_btn, _on_cancel, LV_EVENT_CLICKED, s);
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_font(cancel_label, &ui_font_font0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(cancel_label, lv_color_hex(0x529d53),
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    /* Delete */
    lv_obj_t *delete_btn = lv_button_create(s->container);
    lv_obj_set_size(delete_btn, 100, 50);
    lv_obj_set_align(delete_btn, LV_ALIGN_BOTTOM_LEFT);
    lv_obj_set_style_bg_color(delete_btn, lv_color_hex(0x292831), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(delete_btn, _on_delete, LV_EVENT_CLICKED, s);
    lv_obj_t *delete_label = lv_label_create(delete_btn);
    lv_label_set_text(delete_label, "Delete");
    lv_obj_set_style_text_font(delete_label, &ui_font_font0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(delete_label, lv_color_hex(0xff0000),
                                LV_PART_MAIN | LV_STATE_DEFAULT);

    /* SSID label */
    lv_obj_t *ssid_label = lv_label_create(s->container);
    lv_label_set_text(ssid_label, ssid);
    lv_obj_set_align(ssid_label, LV_ALIGN_CENTER);
    lv_obj_set_y(ssid_label, -20);
    lv_obj_set_style_text_font(ssid_label, &ui_font_font0, LV_PART_MAIN | LV_STATE_DEFAULT);

    ESP_LOGI(TAG, "details dialog open: %s", ssid);
    return s;
}

void wifi_connect_screen_set_dismiss_cb(wifi_connect_screen_t *s,
                                        wifi_connect_dismiss_cb_t on_dismiss,
                                        void *user_data) {
    if(!s) return;
    s->on_dismiss = on_dismiss;
    s->dismiss_user_data = user_data;
}

void wifi_connect_screen_dismiss(wifi_connect_screen_t *s) {
    _dismiss(s);
}
