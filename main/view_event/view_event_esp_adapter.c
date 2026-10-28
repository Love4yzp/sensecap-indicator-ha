#include "view_event_adapter.h"

#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define VIEW_EVENT_QUEUE_SIZE 10
#define VIEW_EVENT_TASK_STACK_SIZE 10240
#define VIEW_EVENT_COMMAND_ATTEMPT_WAIT_MS 5
#define VIEW_EVENT_STATE_DISPATCH_ATTEMPTS 3
#define VIEW_EVENT_STATE_DISPATCH_RETRY_MS 1

ESP_EVENT_DEFINE_BASE(VIEW_EVENT_TRANSPORT_BASE);

enum view_event_transport_id {
    VIEW_EVENT_TRANSPORT_COMMAND = 0,
    VIEW_EVENT_TRANSPORT_STATE,
};

struct view_event_command_envelope {
    enum view_event_id id;
    size_t payload_size;
    union view_event_payload_storage payload;
};

static esp_event_loop_handle_t s_event_loop;
static SemaphoreHandle_t s_lock;

static void view_event_transport_handler(
    void *context, esp_event_base_t base, int32_t id, void *event_data)
{
    (void)context;
    (void)base;

    if (id == VIEW_EVENT_TRANSPORT_COMMAND) {
        const struct view_event_command_envelope *command = event_data;
        const void *payload =
            command->payload_size > 0 ? command->payload.bytes : NULL;
        view_event_internal_dispatch_command(command->id, payload);
    } else if (id == VIEW_EVENT_TRANSPORT_STATE) {
        const size_t *slot = event_data;
        for (unsigned attempt = 0;
             attempt < VIEW_EVENT_STATE_DISPATCH_ATTEMPTS;
             attempt++) {
            if (view_event_internal_dispatch_state(*slot)) {
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(VIEW_EVENT_STATE_DISPATCH_RETRY_MS));
        }
        ESP_LOGE(
            "view-event",
            "state slot %u remained synchronization-busy",
            (unsigned)*slot);
    }
}

bool view_event_adapter_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL) {
        ESP_LOGE("view-event", "failed to create delivery lock");
        return false;
    }

    const esp_event_loop_args_t args = {
        .queue_size = VIEW_EVENT_QUEUE_SIZE,
        .task_name = "view_event_task",
        .task_priority = uxTaskPriorityGet(NULL),
        .task_stack_size = VIEW_EVENT_TASK_STACK_SIZE,
        .task_core_id = tskNO_AFFINITY,
    };

    esp_err_t err = esp_event_loop_create(&args, &s_event_loop);
    if (err != ESP_OK) {
        ESP_LOGE("view-event", "failed to create delivery loop: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return false;
    }

    err = esp_event_handler_instance_register_with(
        s_event_loop,
        VIEW_EVENT_TRANSPORT_BASE,
        ESP_EVENT_ANY_ID,
        view_event_transport_handler,
        NULL,
        NULL);
    if (err != ESP_OK) {
        ESP_LOGE("view-event", "failed to register delivery adapter: %s", esp_err_to_name(err));
        esp_event_loop_delete(s_event_loop);
        s_event_loop = NULL;
        vSemaphoreDelete(s_lock);
        s_lock = NULL;
        return false;
    }
    return true;
}

view_event_adapter_result_t view_event_adapter_post_command(
    enum view_event_id id, const void *payload, size_t payload_size)
{
    struct view_event_command_envelope command = {
        .id = id,
        .payload_size = payload_size,
    };
    if (payload_size > 0) {
        memcpy(command.payload.bytes, payload, payload_size);
    }

    size_t envelope_size = offsetof(struct view_event_command_envelope, payload) + payload_size;
    esp_err_t err = esp_event_post_to(
        s_event_loop,
        VIEW_EVENT_TRANSPORT_BASE,
        VIEW_EVENT_TRANSPORT_COMMAND,
        &command,
        envelope_size,
        pdMS_TO_TICKS(VIEW_EVENT_COMMAND_ATTEMPT_WAIT_MS));

    if (err == ESP_OK) {
        return VIEW_EVENT_ADAPTER_OK;
    }
    if (err == ESP_ERR_TIMEOUT) {
        return VIEW_EVENT_ADAPTER_FULL;
    }
    return VIEW_EVENT_ADAPTER_ERROR;
}

view_event_adapter_result_t view_event_adapter_post_state(size_t slot)
{
    esp_err_t err = esp_event_post_to(
        s_event_loop,
        VIEW_EVENT_TRANSPORT_BASE,
        VIEW_EVENT_TRANSPORT_STATE,
        &slot,
        sizeof(slot),
        0);

    if (err == ESP_OK) {
        return VIEW_EVENT_ADAPTER_OK;
    }
    if (err == ESP_ERR_TIMEOUT) {
        return VIEW_EVENT_ADAPTER_FULL;
    }
    return VIEW_EVENT_ADAPTER_ERROR;
}

bool view_event_adapter_lock(uint32_t timeout_ms)
{
    return s_lock != NULL &&
        xSemaphoreTake(s_lock, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void view_event_adapter_unlock(void)
{
    xSemaphoreGive(s_lock);
}
