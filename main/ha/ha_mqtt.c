#include <stdlib.h>
#include <string.h>

#include "ha.h"
#include "home_assistant_config.h"
#include "mqtt.h"
#include "esp_log.h"
#include "esp_event.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ha-model";

ESP_EVENT_DEFINE_BASE(HA_CFG_EVENT_BASE);
esp_event_loop_handle_t ha_cfg_event_handle;
instance_mqtt mqtt_ha_instance;
static instance_mqtt_t instance_ptr = &mqtt_ha_instance;

static void _mqtt_ha_start(instance_mqtt *instance);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            instance_ptr->mqtt_connected_flag = true;
            ha_sensor_subscribe(client);
            ha_switch_subscribe(client);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            instance_ptr->mqtt_connected_flag = false;
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            ESP_LOGD(TAG, "MQTT_EVENT_DATA: TOPIC=%.*s", event->topic_len, event->topic);
            ESP_LOGD(TAG, "MQTT_EVENT_DATA: DATA=%.*s", event->data_len, event->data);
            ha_sensor_on_mqtt_data(event->topic, event->topic_len, event->data, event->data_len);
            ha_switch_on_mqtt_data(event->topic, event->topic_len, event->data, event->data_len);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
                log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
                log_error_if_nonzero("captured as transport's socket errno", event->error_handle->esp_transport_sock_errno);
                ESP_LOGI(TAG, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));
            }
            break;
        default:
            ESP_LOGW(TAG, "Other event id:%d", event->event_id);
            ESP_LOGW(TAG,
                     "If you are always here, please check that your broker is "
                     "accessible.");
            break;
    }
}

static void handle_wifi_status_change(const struct view_data_wifi_st *wifi_status)
{
    ESP_LOGI(TAG, "WiFi status changed. Connected: %d", wifi_status->is_network);
    if (wifi_status->is_network && instance_ptr->is_using) {
        esp_event_post_to(mqtt_app_event_handle, MQTT_APP_EVENT_BASE, MQTT_APP_START, &instance_ptr, sizeof(instance_mqtt_t), portMAX_DELAY);
    } else {
        /* TODO: Implement MQTT shutdown logic if needed */
    }
}

static void view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    if (id == VIEW_EVENT_WIFI_ST) {
        ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_ST");
        handle_wifi_status_change((struct view_data_wifi_st *)event_data);
    }
}

static void _cfg_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id) {
        case HA_CFG_SET:
            ESP_LOGI(TAG, "event: HA_CFG_BROKER_SET");
            esp_event_post_to(mqtt_app_event_handle, MQTT_APP_EVENT_BASE, MQTT_APP_RESTART, &instance_ptr, sizeof(instance_mqtt_t), portMAX_DELAY);
            break;
        case HA_CFG_BROKER_CHANGED: {
            if (!event_data) {
                break;
            }
            const char *broker_url = (const char *)event_data;
            ESP_LOGI(TAG, "HA_CFG_BROKER_CHANGED: %s", broker_url);
            esp_mqtt_client_stop(instance_ptr->mqtt_client);
            esp_mqtt_client_set_uri(instance_ptr->mqtt_client, broker_url);
            esp_mqtt_client_start(instance_ptr->mqtt_client);
            break;
        }
        default:
            break;
    }
}

static void _mqtt_ha_start(instance_mqtt *instance)
{
    if (!get_mqtt_net_flag()) {
        return;
    }

    if (instance->mqtt_client != NULL) {
        esp_mqtt_client_stop(instance->mqtt_client);
        esp_mqtt_client_destroy(instance->mqtt_client);
        instance->mqtt_client = NULL;
    }

    if (instance->mqtt_cfg != NULL) {
        free(instance->mqtt_cfg);
        instance->mqtt_cfg = NULL;
    }

    ha_cfg_interface hf_cfg;
    if (ha_cfg_get(&hf_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get HA configuration");
        return;
    }

    instance->mqtt_cfg = (esp_mqtt_client_config_t *)malloc(sizeof(esp_mqtt_client_config_t));
    *instance->mqtt_cfg = (esp_mqtt_client_config_t){
        .broker.address.uri = hf_cfg.broker_url,
        .credentials.client_id = hf_cfg.client_id,
        .credentials.username = hf_cfg.username,
        .credentials.authentication.password = hf_cfg.password,
    };

    ESP_LOGI(TAG, "| Broker Address               | %-40s |", hf_cfg.broker_url);
    ESP_LOGI(TAG, "| Client ID                    | %-40s |", hf_cfg.client_id);
    ESP_LOGI(TAG, "| username                     | %-40s |", hf_cfg.username);
    ESP_LOGI(TAG, "| password                     | %-40s |", hf_cfg.password[0] ? "****" : "(not set)");

    instance->mqtt_client = esp_mqtt_client_init(instance->mqtt_cfg);
    if (instance->mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return;
    }
    esp_mqtt_client_register_event(instance->mqtt_client, ESP_EVENT_ANY_ID, instance->mqtt_event_handler, NULL);
    esp_err_t start_result = esp_mqtt_client_start(instance->mqtt_client);
    if (start_result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(start_result));
    } else {
        ESP_LOGI(TAG, "MQTT client started successfully");
    }
}

int indicator_ha_model_init(void)
{
    ha_sensor_init();
    ha_switch_init();

    esp_event_loop_args_t ha_event_task_args = {
        .queue_size = 5,
        .task_name = "ha_event_task",
        .task_priority = uxTaskPriorityGet(NULL),
        .task_stack_size = 4096,
        .task_core_id = tskNO_AFFINITY,
    };

    ESP_ERROR_CHECK(esp_event_loop_create(&ha_event_task_args, &ha_cfg_event_handle));

    ESP_LOGI(TAG, "mqtt_ha_init");

    mqtt_ha_instance = (instance_mqtt){
        .mqtt_name = "ha-model",
        .mqtt_connected_flag = false,
        .mqtt_client = NULL,
        .mqtt_cfg = NULL,
        .mqtt_event_handler = mqtt_event_handler,
        .mqtt_starter = _mqtt_ha_start,
        .is_using = true,
    };

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(ha_cfg_event_handle, HA_CFG_EVENT_BASE, ESP_EVENT_ANY_ID, _cfg_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, view_event_handler, NULL, NULL));
    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_ADDR_DISPLAY, NULL, 0, portMAX_DELAY);
    return ESP_OK;
}

int indicator_ha_view_init(void)
{
    ha_config_view_init();
    ha_switch_screen_t *screen = ha_switch_screen_create();
    ha_switch_attach_screen(screen);
    return ESP_OK;
}
