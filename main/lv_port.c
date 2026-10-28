#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "bsp_board.h"
#include "bsp_lcd.h"
#include "indev/indev.h"
#include "lvgl_health/lvgl_health.h"
#include "lv_port.h"
#include "sdkconfig.h"

#define LV_PORT_BUFFER_HEIGHT        (brd->LCD_HEIGHT)
#define LV_PORT_TASK_DELAY_MS        (5)
#define LV_PORT_TASK_MAX_SLEEP_MS    (500)
#define LV_PORT_TASK_STACK_SIZE      (8192)
#define LV_PORT_HEALTH_HEARTBEAT_MS  (500)

static const char *TAG = "lvgl_port";
static lv_display_t *s_display = NULL;
static lv_indev_t *s_indev_touchpad = NULL;
static lv_indev_t *s_indev_button = NULL;
static lv_timer_t *s_health_timer = NULL;

#ifndef CONFIG_LCD_TASK_PRIORITY
#define CONFIG_LCD_TASK_PRIORITY    5
#endif

static void lv_port_disp_init(void);
static void lv_port_indev_init(void);
static void health_heartbeat_cb(lv_timer_t *timer);
static void button_read(lv_indev_t *indev, lv_indev_data_t *data);
static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data);

void lv_port_init(void)
{
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_priority = CONFIG_LCD_TASK_PRIORITY;
    lvgl_cfg.task_stack = LV_PORT_TASK_STACK_SIZE;
    lvgl_cfg.task_affinity = -1;
    lvgl_cfg.task_max_sleep_ms = LV_PORT_TASK_MAX_SLEEP_MS;
    lvgl_cfg.timer_period_ms = LV_PORT_TASK_DELAY_MS;

    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));
    lv_port_disp_init();
    lv_port_indev_init();

    if (lvgl_port_lock(1000)) {
        s_health_timer = lv_timer_create(
            health_heartbeat_cb, LV_PORT_HEALTH_HEARTBEAT_MS, NULL);
        lvgl_port_unlock();
    }
    if (s_health_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create LVGL health heartbeat");
        return;
    }

    const esp_err_t health_result = lvgl_health_start();
    if (health_result != ESP_OK) {
        ESP_LOGE(
            TAG,
            "Failed to start LVGL health diagnostics: %s",
            esp_err_to_name(health_result));
        if (lvgl_port_lock(1000)) {
            lv_timer_delete(s_health_timer);
            s_health_timer = NULL;
            lvgl_port_unlock();
        }
        return;
    }

    const esp_err_t wake_result =
        lvgl_port_task_wake(LVGL_PORT_EVENT_USER, NULL);
    if (wake_result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Failed to wake LVGL task after health start: %s",
            esp_err_to_name(wake_result));
    }
}

void lv_port_sem_take(void)
{
    lvgl_port_lock(0);
}

void lv_port_sem_give(void)
{
    lvgl_port_unlock();
}

static void health_heartbeat_cb(lv_timer_t *timer)
{
    (void)timer;
    lvgl_health_heartbeat();
}

static void button_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    static uint32_t last_key = 0;

    indev_data_t indev_data;
    if (ESP_OK != indev_get_major_value(&indev_data)) {
        ESP_LOGE(TAG, "Failed read input device value");
        return;
    }

    if (indev_data.btn_val & 0x02) {
        last_key = LV_KEY_ENTER;
        data->state = LV_INDEV_STATE_PRESSED;
        ESP_LOGD(TAG, "ok");
    } else if (indev_data.btn_val & 0x04) {
        last_key = LV_KEY_PREV;
        data->state = LV_INDEV_STATE_PRESSED;
        ESP_LOGD(TAG, "prev");
    } else if (indev_data.btn_val & 0x01) {
        last_key = LV_KEY_NEXT;
        data->state = LV_INDEV_STATE_PRESSED;
        ESP_LOGD(TAG, "next");
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    data->key = last_key;
}

static void touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    static int32_t x = 0;
    static int32_t y = 0;

    indev_data_t indev_data;
    if (ESP_OK != indev_get_major_value(&indev_data)) {
        return;
    }

    if (indev_data.pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = CONFIG_LCD_EVB_SCREEN_WIDTH - indev_data.x;
        data->point.y = CONFIG_LCD_EVB_SCREEN_HEIGHT - indev_data.y;
        x = data->point.x;
        y = data->point.y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point.x = x;
        data->point.y = y;
    }
}

static void lv_port_disp_init(void)
{
    const board_res_desc_t *brd = bsp_board_get_description();
    const uint32_t buffer_size = brd->LCD_WIDTH * LV_PORT_BUFFER_HEIGHT;

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = bsp_lcd_get_io_handle(),
        .panel_handle = bsp_lcd_get_panel_handle(),
        .buffer_size = buffer_size,
        .double_buffer =
#if CONFIG_LCD_AVOID_TEAR
            true,
#else
            false,
#endif
        .hres = brd->LCD_WIDTH,
        .vres = brd->LCD_HEIGHT,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = brd->LCD_SWAP_XY,
            .mirror_x = brd->LCD_MIRROR_X,
            .mirror_y = brd->LCD_MIRROR_Y,
        },
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
#if CONFIG_LCD_LVGL_FULL_REFRESH
            .full_refresh = true,
#endif
#if CONFIG_LCD_LVGL_DIRECT_MODE
            .direct_mode = true,
#endif
            .swap_bytes = false,
        },
    };

    if (LCD_IFACE_RGB == brd->LCD_IFACE) {
        const lvgl_port_display_rgb_cfg_t rgb_cfg = {
            .flags = {
                .bb_mode = true,
#if CONFIG_LCD_AVOID_TEAR
                .avoid_tearing = true,
#else
                .avoid_tearing = false,
#endif
            },
        };
        s_display = lvgl_port_add_disp_rgb(&disp_cfg, &rgb_cfg);
    } else {
        s_display = lvgl_port_add_disp(&disp_cfg);
    }

    ESP_ERROR_CHECK(s_display == NULL ? ESP_FAIL : ESP_OK);
}

static void lv_port_indev_init(void)
{
    const board_res_desc_t *brd = bsp_board_get_description();

    if (brd->BSP_INDEV_IS_TP) {
        ESP_LOGI(TAG, "Add TP input device to LVGL");
        s_indev_touchpad = lv_indev_create();
        ESP_ERROR_CHECK(s_indev_touchpad == NULL ? ESP_FAIL : ESP_OK);
        lv_indev_set_type(s_indev_touchpad, LV_INDEV_TYPE_POINTER);
        lv_indev_set_display(s_indev_touchpad, s_display);
        lv_indev_set_read_cb(s_indev_touchpad, touchpad_read);
    } else {
        ESP_LOGI(TAG, "Add KEYPAD input device to LVGL");
        s_indev_button = lv_indev_create();
        ESP_ERROR_CHECK(s_indev_button == NULL ? ESP_FAIL : ESP_OK);
        lv_indev_set_type(s_indev_button, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_display(s_indev_button, s_display);
        lv_indev_set_read_cb(s_indev_button, button_read);
    }

#if CONFIG_LV_PORT_SHOW_MOUSE_CURSOR
    LV_IMAGE_DECLARE(mouse_cursor_icon)
    lv_obj_t *cursor_obj = lv_image_create(lv_screen_active());
    lv_image_set_src(cursor_obj, &mouse_cursor_icon);
    lv_indev_set_cursor(s_indev_touchpad, cursor_obj);
#endif
}
