#include "mqtt.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "view_event.h"

static const char *TAG = "INDICATOR_MQTT";

ESP_EVENT_DEFINE_BASE(MQTT_APP_EVENT_BASE);
esp_event_loop_handle_t mqtt_app_event_handle;

bool mqtt_net_flag = false;

bool get_mqtt_net_flag(void) {
    return mqtt_net_flag;
}

static void _wifi_event_handler(
    void *handler_args, enum view_event_id id, const void *event_data) {
    switch (id) {
        case VIEW_EVENT_WIFI_ST: {
            struct view_data_wifi_st *p_st = (struct view_data_wifi_st *)event_data;
            mqtt_net_flag = p_st->is_network;
            ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_ST is_network:%d\tmqtt_net_flag:%d",
                     p_st->is_network, mqtt_net_flag);
            break;
        }
        default:
            break;
    }
}

static void mqtt_start_interface(const instance_mqtt *instance, enum MQTT_APP_EVENT flag) {
    if (!mqtt_net_flag || !instance || !instance->mqtt_name || !instance->mqtt_starter) {
        ESP_LOGE(TAG, "Cannot start MQTT: %s",
                 !mqtt_net_flag ? "No network" : "Invalid instance");
        return;
    }

    if (flag == MQTT_APP_START && instance->mqtt_connected_flag) {
        ESP_LOGW(TAG, "%s is already connected.", instance->mqtt_name);
        return;
    }

    if (flag == MQTT_APP_RESTART && instance->mqtt_client) {
        esp_mqtt_client_stop(instance->mqtt_client);
        esp_mqtt_client_destroy(instance->mqtt_client);
    }

    instance->mqtt_starter(instance);
}

static void _app_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    instance_mqtt_t instance = *(instance_mqtt_t *)event_data;
    if (!instance) {
        ESP_LOGE(TAG, "Invalid MQTT instance");
        return;
    }
    switch (id) {
        case MQTT_APP_START:
        case MQTT_APP_RESTART:
            if (mqtt_net_flag && instance->is_using)
                mqtt_start_interface(instance, id);
            break;
        case MQTT_APP_STOP:
            if (instance->mqtt_connected_flag && instance->mqtt_client) {
                esp_mqtt_client_stop(instance->mqtt_client);
                esp_mqtt_client_destroy(instance->mqtt_client);
                instance->mqtt_client = NULL;
                instance->mqtt_connected_flag = false;
            }
            break;
        default:
            ESP_LOGW(TAG, "Unknown MQTT app event: %ld", id);
            break;
    }
}

int indicator_mqtt_init(void) {
    esp_event_loop_args_t mqtt_event_task_args = {
        .queue_size = 5,
        .task_name = "mqtt_event_task",
        .task_priority = uxTaskPriorityGet(NULL),
        .task_stack_size = 4096,
        .task_core_id = tskNO_AFFINITY
    };
    ESP_ERROR_CHECK(esp_event_loop_create(&mqtt_event_task_args, &mqtt_app_event_handle));

    ESP_ERROR_CHECK(view_event_subscribe(
        VIEW_EVENT_WIFI_ST, _wifi_event_handler, NULL) ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(
        mqtt_app_event_handle, MQTT_APP_EVENT_BASE, ESP_EVENT_ANY_ID,
        _app_event_handler, NULL, NULL));

    return ESP_OK;
}

void log_error_if_nonzero(const char *message, int error_code) {
    if (error_code != 0)
        ESP_LOGE(TAG, "Last error %s: 0x%x", message, error_code);
}
