#include "mqtt.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "view_event.h"

static const char *TAG = "INDICATOR_MQTT";

ESP_EVENT_DEFINE_BASE(MQTT_APP_EVENT_BASE);
esp_event_loop_handle_t mqtt_app_event_handle;

static bool mqtt_net_flag = false;
static portMUX_TYPE s_control_lock = portMUX_INITIALIZER_UNLOCKED;

bool get_mqtt_net_flag(void) {
    return mqtt_net_flag;
}

static void mqtt_generation_advance(instance_mqtt *instance) {
    portENTER_CRITICAL(&s_control_lock);
    instance->generation++;
    portEXIT_CRITICAL(&s_control_lock);
}

static void mqtt_control_wake(instance_mqtt *instance) {
    if (!mqtt_app_event_handle) {
        ESP_LOGE(TAG, "MQTT control is pending until the lifecycle task starts");
        return;
    }
    esp_err_t err = esp_event_post_to(
        mqtt_app_event_handle, MQTT_APP_EVENT_BASE, MQTT_APP_CONTROL,
        &instance, sizeof(instance), 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MQTT control wake deferred; mailbox remains pending");
    }
}

void mqtt_instance_request(instance_mqtt *instance, enum MQTT_APP_EVENT event) {
    if (!instance || (event != MQTT_APP_START && event != MQTT_APP_RESTART &&
                      event != MQTT_APP_STOP)) {
        return;
    }

    portENTER_CRITICAL(&s_control_lock);
    if (!instance->control.lifecycle_pending ||
        event == MQTT_APP_STOP || event == MQTT_APP_RESTART ||
        instance->control.lifecycle_event == MQTT_APP_STOP) {
        instance->control.lifecycle_event = event;
    }
    instance->control.lifecycle_pending = true;
    portEXIT_CRITICAL(&s_control_lock);
    mqtt_control_wake(instance);
}

void mqtt_instance_connected(
    instance_mqtt *instance, esp_mqtt_client_handle_t client) {
    if (!instance || !client) {
        return;
    }

    portENTER_CRITICAL(&s_control_lock);
    instance->control.connected = (mqtt_client_event_t){
        .instance = instance,
        .client = client,
        .generation = instance->generation,
    };
    instance->control.connected_pending = true;
    portEXIT_CRITICAL(&s_control_lock);
    mqtt_control_wake(instance);
}

void mqtt_instance_stop(instance_mqtt *instance) {
    if (!instance) {
        return;
    }

    portENTER_CRITICAL(&s_control_lock);
    esp_mqtt_client_handle_t client = instance->mqtt_client;
    instance->generation++;
    instance->mqtt_connected_flag = false;
    instance->mqtt_client = NULL;
    portEXIT_CRITICAL(&s_control_lock);

    if (client) {
        (void)esp_mqtt_client_stop(client);
        esp_err_t err = esp_mqtt_client_destroy(client);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to destroy MQTT client: %d", err);
        }
    }
    if (instance->mqtt_stopper) {
        instance->mqtt_stopper(instance);
    }
}

esp_err_t mqtt_publish_enqueue(
    instance_mqtt *instance, const char *topic, const char *data, size_t data_len) {
    if (!instance || !topic || !data) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!mqtt_app_event_handle) {
        return ESP_ERR_INVALID_STATE;
    }

    mqtt_data_t message = {.instance = instance, .data_len = data_len};
    size_t topic_len = strnlen(topic, sizeof(message.topic));
    if (topic_len == 0 || topic_len == sizeof(message.topic)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (data_len > sizeof(message.data)) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(message.topic, topic, topic_len + 1);
    memcpy(message.data, data, data_len);

    esp_err_t err = esp_event_post_to(
        mqtt_app_event_handle, MQTT_APP_EVENT_BASE, MQTT_APP_PUBLISH,
        &message, sizeof(message), 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "MQTT publish queue full or unavailable: %d", err);
    }
    return err;
}

static void _wifi_event_handler(
    void *handler_args, enum view_event_id id, const void *event_data) {
    if (id == VIEW_EVENT_WIFI_ST && event_data) {
        const struct view_data_wifi_st *p_st = event_data;
        mqtt_net_flag = p_st->has_ip;
        ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_ST has_ip:%d is_network:%d",
                 p_st->has_ip, p_st->is_network);
    }
}

static void mqtt_start_interface(instance_mqtt *instance, enum MQTT_APP_EVENT flag) {
    if (!mqtt_net_flag || !instance || !instance->mqtt_name ||
        !instance->mqtt_starter || !instance->is_using) {
        ESP_LOGE(TAG, "Cannot start MQTT: %s",
                 !mqtt_net_flag ? "No LAN IP" : "Invalid or disabled instance");
        return;
    }

    if (flag == MQTT_APP_START && instance->mqtt_client) {
        return;
    }

    mqtt_generation_advance(instance);
    instance->mqtt_starter(instance);
}

static void mqtt_connected_interface(const mqtt_client_event_t *connected) {
    instance_mqtt *instance = connected->instance;
    if (!instance || instance->mqtt_client != connected->client ||
        instance->generation != connected->generation ||
        !instance->mqtt_connected_flag) {
        return;
    }
    if (instance->mqtt_connected_handler) {
        instance->mqtt_connected_handler(instance);
    }
}

static void mqtt_control_dispatch(instance_mqtt *instance) {
    if (!instance) {
        return;
    }

    portENTER_CRITICAL(&s_control_lock);
    mqtt_control_mailbox_t pending = instance->control;
    memset(&instance->control, 0, sizeof(instance->control));
    portEXIT_CRITICAL(&s_control_lock);

    if (pending.lifecycle_pending) {
        if (pending.lifecycle_event == MQTT_APP_STOP) {
            mqtt_instance_stop(instance);
        } else {
            mqtt_start_interface(instance, pending.lifecycle_event);
        }
    }
    if (pending.connected_pending) {
        mqtt_connected_interface(&pending.connected);
    }
}

static void mqtt_publish_interface(const mqtt_data_t *message) {
    instance_mqtt *instance = message->instance;
    if (!instance || !instance->mqtt_client || !instance->mqtt_connected_flag) {
        ESP_LOGW(TAG, "MQTT publish dropped while stopped or disconnected");
        return;
    }

    int message_id = esp_mqtt_client_publish(
        instance->mqtt_client, message->topic, message->data,
        (int)message->data_len, 0, 0);
    if (message_id < 0) {
        ESP_LOGW(TAG, "MQTT publish failed: %d", message_id);
    }
}

static void _app_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data) {
    if (!event_data) {
        ESP_LOGE(TAG, "Missing MQTT event payload");
        return;
    }

    instance_mqtt *instance = id == MQTT_APP_PUBLISH
        ? ((mqtt_data_t *)event_data)->instance
        : *(instance_mqtt_t *)event_data;
    mqtt_control_dispatch(instance);

    switch (id) {
        case MQTT_APP_CONTROL:
            break;
        case MQTT_APP_PUBLISH:
            mqtt_publish_interface(event_data);
            break;
        case MQTT_APP_START:
        case MQTT_APP_RESTART:
            mqtt_start_interface(instance, id);
            break;
        case MQTT_APP_STOP:
            mqtt_instance_stop(instance);
            break;
        default:
            ESP_LOGW(TAG, "Unknown MQTT app event: %ld", id);
            break;
    }
}

int indicator_mqtt_init(void) {
    esp_event_loop_args_t mqtt_event_task_args = {
        .queue_size = MQTT_APP_EVENT_QUEUE_SIZE,
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
