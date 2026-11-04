#include <stdlib.h>
#include <string.h>

#include "ha.h"
#include "ha_switch_screen.h"
#include "ha_tls.h"
#include "home_assistant_config.h"
#include "mqtt.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_crt_bundle.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "view_event.h"

static const char *TAG = "ha-model";

ESP_EVENT_DEFINE_BASE(HA_CFG_EVENT_BASE);
esp_event_loop_handle_t ha_cfg_event_handle;
instance_mqtt mqtt_ha_instance;
static instance_mqtt_t instance_ptr = &mqtt_ha_instance;

/* CA PEM for the active client. esp-mqtt stores the certificate POINTER (it
 * does not copy cert buffers the way it copies credential strings), so this
 * must outlive the client; it is freed and reloaded on every (re)start. */
static char *s_tls_ca = NULL;

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
            mqtt_instance_connected(instance_ptr, client);
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

static void _mqtt_ha_connected(instance_mqtt *instance)
{
    ha_sensor_subscribe(instance->mqtt_client);
    ha_switch_subscribe(instance->mqtt_client);
}

static void _mqtt_ha_stop(instance_mqtt *instance)
{
    if (instance->mqtt_cfg != NULL) {
        free(instance->mqtt_cfg);
        instance->mqtt_cfg = NULL;
    }
    if (s_tls_ca != NULL) {
        free(s_tls_ca);
        s_tls_ca = NULL;
    }
}

static void _mqtt_ha_post_instance_event(enum MQTT_APP_EVENT event)
{
    mqtt_instance_request(instance_ptr, event);
}

static void _mqtt_ha_reconfigure(void)
{
    instance_ptr->is_using = ha_mqtt_enabled_get();
    enum MQTT_APP_EVENT event = instance_ptr->is_using && get_mqtt_net_flag()
        ? MQTT_APP_RESTART : MQTT_APP_STOP;
    _mqtt_ha_post_instance_event(event);
}

static void handle_wifi_status_change(const struct view_data_wifi_st *wifi_status)
{
    if (wifi_status == NULL) {
        return;
    }
    ESP_LOGI(TAG, "WiFi status changed. LAN IP: %d, Internet: %d",
             wifi_status->has_ip, wifi_status->is_network);
    instance_ptr->is_using = ha_mqtt_enabled_get();
    enum MQTT_APP_EVENT event = wifi_status->has_ip && instance_ptr->is_using
        ? MQTT_APP_START : MQTT_APP_STOP;
    _mqtt_ha_post_instance_event(event);
}

static void view_event_handler(
    void *handler_args, enum view_event_id id, const void *event_data)
{
    if (id == VIEW_EVENT_WIFI_ST) {
        handle_wifi_status_change((const struct view_data_wifi_st *)event_data);
    }
}

static void _cfg_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    if (id == HA_CFG_SET || id == HA_CFG_BROKER_CHANGED) {
        _mqtt_ha_reconfigure();
    }
}

esp_err_t ha_mqtt_set_enabled(bool enabled)
{
    esp_err_t err = ha_mqtt_enabled_set(enabled);
    if (err != ESP_OK) {
        return err;
    }
    instance_ptr->is_using = enabled;
    _mqtt_ha_reconfigure();
    return ESP_OK;
}

static void _mqtt_ha_start(instance_mqtt *instance)
{
    mqtt_instance_stop(instance);
    if (!get_mqtt_net_flag() || !instance->is_using || !ha_mqtt_enabled_get()) {
        return;
    }

    ha_cfg_interface hf_cfg;
    if (ha_cfg_get_stored(&hf_cfg) != ESP_OK) {
        ESP_LOGW(TAG, "MQTT has no valid stored broker configuration");
        return;
    }

    instance->mqtt_cfg = (esp_mqtt_client_config_t *)malloc(sizeof(esp_mqtt_client_config_t));
    if (instance->mqtt_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to allocate MQTT configuration");
        return;
    }
    *instance->mqtt_cfg = (esp_mqtt_client_config_t){
        .broker.address.uri = hf_cfg.broker_url,
        .credentials.client_id = hf_cfg.client_id,
        .credentials.username = hf_cfg.username,
        .credentials.authentication.password = hf_cfg.password,
    };

    /* TLS trust wiring — only for mqtts:// URLs; plaintext mqtt:// is untouched.
     * esp-mqtt picks port 8883 automatically when an mqtts URI carries no port. */
    bool tls = ha_tls_url_is_tls(hf_cfg.broker_url);
    ha_tls_mode_t tls_mode = HA_TLS_MODE_VERIFY;
    size_t ca_len = 0;
    if (tls) {
        tls_mode = ha_tls_mode_get();
        if (tls_mode == HA_TLS_MODE_INSECURE) {
            /* No trust source configured: with CONFIG_ESP_TLS_INSECURE +
             * CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY (sdkconfig.defaults),
             * esp-tls encrypts the session but skips server verification. */
            ESP_LOGW(TAG, "TLS INSECURE mode: server certificate is NOT verified");
        } else {
            s_tls_ca = ha_tls_ca_load(&ca_len);
            if (s_tls_ca != NULL) {
                instance->mqtt_cfg->broker.verification.certificate = s_tls_ca;
            } else {
                instance->mqtt_cfg->broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
            }
        }
    }

    ESP_LOGI(TAG, "| Broker Address               | %-40s |", hf_cfg.broker_url);
    ESP_LOGI(TAG, "| TLS                          | %-40s |",
             !tls                                ? "off (mqtt://)"
             : tls_mode == HA_TLS_MODE_INSECURE  ? "on, INSECURE (no verification)"
             : s_tls_ca                          ? "on, verify: stored CA"
                                                 : "on, verify: public CA bundle");
    ESP_LOGI(TAG, "| Client ID                    | %-40s |", hf_cfg.client_id);
    ESP_LOGI(TAG, "| username                     | %-40s |", hf_cfg.username);
    ESP_LOGI(TAG, "| password                     | %-40s |", hf_cfg.password[0] ? "****" : "(not set)");

    instance->mqtt_client = esp_mqtt_client_init(instance->mqtt_cfg);
    if (instance->mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        mqtt_instance_stop(instance);
        return;
    }
    esp_err_t register_result = esp_mqtt_client_register_event(
        instance->mqtt_client, ESP_EVENT_ANY_ID, instance->mqtt_event_handler, NULL);
    if (register_result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(register_result));
        mqtt_instance_stop(instance);
        return;
    }
    esp_err_t start_result = esp_mqtt_client_start(instance->mqtt_client);
    if (start_result != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(start_result));
        mqtt_instance_stop(instance);
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
        .mqtt_stopper = _mqtt_ha_stop,
        .mqtt_connected_handler = _mqtt_ha_connected,
        .is_using = ha_mqtt_enabled_get(),
    };

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(ha_cfg_event_handle, HA_CFG_EVENT_BASE, ESP_EVENT_ANY_ID, _cfg_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(view_event_subscribe(
        VIEW_EVENT_WIFI_ST, view_event_handler, NULL) ? ESP_OK : ESP_FAIL);
    view_event_publish_state(VIEW_EVENT_HA_ADDR_DISPLAY, NULL, 0);
    return ESP_OK;
}

int indicator_ha_view_init(void)
{
    ha_config_view_init();
    ha_switch_screen_create();
    return ESP_OK;
}
