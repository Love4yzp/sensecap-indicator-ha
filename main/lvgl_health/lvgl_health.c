#include "lvgl_health.h"

#include <stdbool.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lvgl_health_state.h"

#define LVGL_HEALTH_MONITOR_PERIOD_MS 1000U
#define LVGL_HEALTH_MONITOR_STACK_SIZE 3072U
#define LVGL_HEALTH_MONITOR_PRIORITY 2U
#define LVGL_HEALTH_LOW_STACK_BYTES 1024U

static const char *TAG = "lvgl-health";

static portMUX_TYPE s_snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static TaskHandle_t s_lvgl_task;
static TaskHandle_t s_monitor_task;
static uint32_t s_heartbeat_sequence;

static uint64_t now_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000);
}

static const char *task_state_name(eTaskState state)
{
    switch (state) {
        case eRunning:
            return "running";
        case eReady:
            return "ready";
        case eBlocked:
            return "blocked";
        case eSuspended:
            return "suspended";
        case eDeleted:
            return "deleted";
        case eInvalid:
        default:
            return "invalid";
    }
}

void lvgl_health_heartbeat(void)
{
    TaskHandle_t current_task = xTaskGetCurrentTaskHandle();
    TaskHandle_t expected_task = NULL;
    bool captured = false;
    bool changed = false;

    portENTER_CRITICAL(&s_snapshot_lock);
    if (s_lvgl_task == NULL) {
        s_lvgl_task = current_task;
        captured = true;
    } else if (s_lvgl_task != current_task) {
        expected_task = s_lvgl_task;
        changed = true;
    }
    s_heartbeat_sequence++;
    portEXIT_CRITICAL(&s_snapshot_lock);

    if (captured) {
        ESP_LOGI(
            TAG,
            "captured actual LVGL task handle=%p",
            (void *)current_task);
    } else if (changed) {
        ESP_LOGE(
            TAG,
            "LVGL heartbeat changed task handle: expected=%p actual=%p",
            (void *)expected_task,
            (void *)current_task);
    }
}

static void snapshot(
    TaskHandle_t *lvgl_task,
    uint32_t *heartbeat_sequence)
{
    portENTER_CRITICAL(&s_snapshot_lock);
    *lvgl_task = s_lvgl_task;
    *heartbeat_sequence = s_heartbeat_sequence;
    portEXIT_CRITICAL(&s_snapshot_lock);
}

static void log_task_health(
    const char *message,
    TaskHandle_t lvgl_task,
    uint64_t stalled_ms)
{
    if (lvgl_task == NULL) {
        ESP_LOGW(
            TAG,
            "%s: heartbeat_age_ms=%llu task_handle=unavailable",
            message,
            (unsigned long long)stalled_ms);
        return;
    }

    const eTaskState task_state = eTaskGetState(lvgl_task);
    const UBaseType_t stack_low_water =
        task_state == eDeleted || task_state == eInvalid ?
        0 :
        uxTaskGetStackHighWaterMark(lvgl_task);
    ESP_LOGW(
        TAG,
        "%s: heartbeat_age_ms=%llu task_state=%s stack_low_water=%u",
        message,
        (unsigned long long)stalled_ms,
        task_state_name(task_state),
        (unsigned)stack_low_water);
}

static void monitor_task(void *context)
{
    (void)context;

    lvgl_health_state_t state;
    lvgl_health_state_init(&state, now_ms());
    bool low_stack_reported = false;

    while (true) {
        TaskHandle_t lvgl_task = NULL;
        uint32_t heartbeat_sequence = 0;
        snapshot(&lvgl_task, &heartbeat_sequence);

        const uint64_t observed_ms = now_ms();
        const uint64_t stalled_ms = observed_ms - state.last_progress_ms;
        const lvgl_health_action_t action = lvgl_health_state_observe(
            &state, observed_ms, heartbeat_sequence);

        if (action == LVGL_HEALTH_ACTION_STALLED) {
            log_task_health("LVGL heartbeat stalled", lvgl_task, stalled_ms);
        } else if (action == LVGL_HEALTH_ACTION_ESCALATED) {
            log_task_health(
                "LVGL heartbeat still stalled; manual diagnosis required",
                lvgl_task,
                stalled_ms);
        } else if (action == LVGL_HEALTH_ACTION_RECOVERED) {
            ESP_LOGI(
                TAG,
                "LVGL heartbeat recovered after %llu ms",
                (unsigned long long)stalled_ms);
        }

        if (lvgl_task != NULL && !low_stack_reported) {
            const eTaskState task_state = eTaskGetState(lvgl_task);
            if (task_state == eDeleted || task_state == eInvalid) {
                vTaskDelay(pdMS_TO_TICKS(LVGL_HEALTH_MONITOR_PERIOD_MS));
                continue;
            }
            const UBaseType_t stack_low_water =
                uxTaskGetStackHighWaterMark(lvgl_task);
            if (stack_low_water < LVGL_HEALTH_LOW_STACK_BYTES) {
                ESP_LOGW(
                    TAG,
                    "LVGL task stack low-water mark is %u bytes",
                    (unsigned)stack_low_water);
                low_stack_reported = true;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(LVGL_HEALTH_MONITOR_PERIOD_MS));
    }
}

esp_err_t lvgl_health_start(void)
{
    if (s_monitor_task != NULL) {
        return ESP_OK;
    }

    const BaseType_t created = xTaskCreate(
        monitor_task,
        "lvgl_health",
        LVGL_HEALTH_MONITOR_STACK_SIZE,
        NULL,
        LVGL_HEALTH_MONITOR_PRIORITY,
        &s_monitor_task);
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
