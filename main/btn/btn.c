#include "btn.h"
#include "button_press_state.h"
#include "bsp_board.h"
#include "bsp_btn.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "view_event.h"
#include <nvs.h>
#include <nvs_flash.h>

static bool sleep_flag = false;

static esp_timer_handle_t factory_reset_timer_handle;

static const char *TAG = "btn";

__attribute__((weak)) int indicator_display_off(void) {
    ESP_LOGI(TAG, "indicator_display_off");
}

__attribute__((weak)) int indicator_display_on(void) {
    ESP_LOGI(TAG, "indicator_display_on");
}

__attribute__((weak)) bool indicator_display_st_get(void) {
    ESP_LOGI(TAG, "indicator_display_st_get");
    static bool st = false;
    return st;
}

static void _factory_reset_callback(void *arg) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    fflush(stdout);
    esp_restart();
}

static void _btn_click_callback(void *arg) {
    bool st = 0;
    if (sleep_flag) {
        ESP_LOGI(TAG, "click, cur st: sleep mode, restart!");
        fflush(stdout);
        esp_restart();
        return;
    }
    if (indicator_display_st_get()) {
        ESP_LOGI(TAG, "click, off");
        indicator_display_off();
        st = 0;
        view_event_publish_state(VIEW_EVENT_SCREEN_CTRL, &st, sizeof(st));
    } else {
        ESP_LOGI(TAG, "click, on");
        st = 1;
        view_event_publish_state(VIEW_EVENT_SCREEN_CTRL, &st, sizeof(st));
        indicator_display_on();
    }
}

static void _btn_double_click_callback(void *arg) {
    ESP_LOGI(TAG, "double click");
}

static void _btn_press_start_callback(void *arg) {
    ESP_LOGI(TAG, "press start");
    button_press_begin();
}

static void _btn_long_press_hold_callback(void *arg) {
    button_press_action_t action =
        button_press_hold(CONFIG_BUTTON_PERIOD_TIME_MS, sleep_flag);

    switch (action) {
        case BUTTON_PRESS_ACTION_RESTART:
            ESP_LOGI(TAG, "wake, restart");
            fflush(stdout);
            esp_restart();
            break;
        case BUTTON_PRESS_ACTION_PREPARE_SLEEP: {
            ESP_LOGI(TAG, "long_press: entry sleep mode");
            indicator_display_off();
            bool st = 0;
            view_event_publish_state(VIEW_EVENT_SCREEN_CTRL, &st, sizeof(st));
            break;
        }
        case BUTTON_PRESS_ACTION_FACTORY_RESET: {
            ESP_LOGI(TAG, "factory reset");
            view_event_delivery_t delivery =
                view_event_post_command(VIEW_EVENT_FACTORY_RESET, NULL, 0);
            if (delivery != VIEW_EVENT_DELIVERY_ENQUEUED) {
                ESP_LOGE(
                    TAG,
                    "factory reset command rejected: delivery=%d",
                    (int)delivery);
            }
            bool st = 1;
            view_event_publish_state(VIEW_EVENT_SCREEN_CTRL, &st, sizeof(st));
            indicator_display_on();

            const esp_timer_create_args_t timer_args = {
                .callback = &_factory_reset_callback,
                .arg = (void *)factory_reset_timer_handle,
                .name = "factory_reset"
            };
            ESP_ERROR_CHECK(esp_timer_create(&timer_args, &factory_reset_timer_handle));
            ESP_ERROR_CHECK(esp_timer_start_once(factory_reset_timer_handle, 1000000 * 3));
            break;
        }
        case BUTTON_PRESS_ACTION_NONE:
        case BUTTON_PRESS_ACTION_SHUTDOWN:
        default:
            break;
    }
}

static void _btn_press_up_callback(void *arg) {
    if (button_press_release() == BUTTON_PRESS_ACTION_SHUTDOWN) {
        view_event_delivery_t delivery =
            view_event_post_command(VIEW_EVENT_SHUTDOWN, NULL, 0);
        if (delivery == VIEW_EVENT_DELIVERY_ENQUEUED) {
            ESP_LOGI(TAG, "entry sleep mode");
            button_press_shutdown_accepted();
            sleep_flag = true;
        } else {
            ESP_LOGE(
                TAG,
                "shutdown command rejected; staying awake: delivery=%d",
                (int)delivery);
            bool st = 1;
            view_event_publish_state(VIEW_EVENT_SCREEN_CTRL, &st, sizeof(st));
            indicator_display_on();
        }
    }
}

int indicator_btn_init(void) {
    bsp_btn_register_callback(BOARD_BTN_ID_USER, BUTTON_SINGLE_CLICK, _btn_click_callback, NULL);
    bsp_btn_register_callback(BOARD_BTN_ID_USER, BUTTON_DOUBLE_CLICK, _btn_double_click_callback, NULL);
    bsp_btn_register_callback(BOARD_BTN_ID_USER, BUTTON_LONG_PRESS_START, _btn_press_start_callback, NULL);
    bsp_btn_register_callback(BOARD_BTN_ID_USER, BUTTON_LONG_PRESS_HOLD, _btn_long_press_hold_callback, NULL);
    bsp_btn_register_callback(BOARD_BTN_ID_USER, BUTTON_PRESS_UP, _btn_press_up_callback, NULL);
    return ESP_OK;
}
