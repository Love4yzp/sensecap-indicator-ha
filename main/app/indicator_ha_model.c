#include <string.h>

#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "home_assistant_config.h"
#include "indicator_cmd.h"
#include "indicator_ha.h"
#include "indicator_mqtt.h"
#include "indicator_storage_nvs.h"
#include "ui.h"

#define HA_CFG_STORAGE   "ha-cfg"
#define MAX_DATA_BUF_LEN 64

static const char* TAG = "ha-model";

typedef struct ha_sensor_entity {
    int   index;
    char* key;
    char* topic;
    int   qos;
} ha_sensor_entity_t;

typedef struct ha_switch_entity {
    int   index;
    char* key;
    char* topic_set;
    char* topic_state;
    int   qos;
} ha_switch_entity_t;

// ui  {"index": 1, value: "27.2"}  <==MQTT==  topic:/xxx/state {"key": "27.2"}  HA
ha_sensor_entity_t ha_sensor_entities[CONFIG_HA_SENSOR_ENTITY_NUM];

// ui  {"index": 1, value: 1}  <==MQTT==  topic:/xxx/set    {"key": 1}  HA
// ui  {"index": 1, value: 1}  ==MQTT==>  topic:/xxx/state  {"key": 1}  HA
ha_switch_entity_t ha_switch_entities[CONFIG_HA_SWITCH_ENTITY_NUM];

static int switch_state[CONFIG_HA_SWITCH_ENTITY_NUM];

/****************** MQTT Configutation ******************/
static void _mqtt_ha_start(instance_mqtt* instance);
static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);

static void ha_entities_init(void);
static void ha_ctrl_cfg_restore(void);
static void ha_ctrl_cfg_save(void);
static int  mqtt_msg_handler(const char* topic, int topic_len, const char* data, int data_len);

static TaskHandle_t restore_task_handle = NULL;
static void         restore_data_task(void* args);

/*Instance*/
instance_mqtt          mqtt_ha_instance;  // global entrance
static instance_mqtt_t instance_ptr = &mqtt_ha_instance;

static void ha_entites_init(void)
{
    const char* sensor_keys[]         = CONFIG_SENSOR_VALUE_KEYS;
    const char* sensor_topics[]       = CONFIG_SENSOR_TOPICS;
    const char* switch_keys[]         = CONFIG_SWITCH_VALUE_KEYS;
    const char* switch_topics_state[] = CONFIG_SWITCH_TOPICS_STATE;
    const char* switch_topics_set[]   = CONFIG_SWITCH_TOPICS_SET;

    // Initialize sensor entities
    for (int i = 0; i < CONFIG_HA_SENSOR_ENTITY_NUM; i++) {
        ha_sensor_entities[i] = (ha_sensor_entity_t){.index = i, .key = sensor_keys[i], .topic = sensor_topics[i], .qos = CONFIG_TOPIC_SENSOR_DATA_QOS};
    }

    // Initialize switch entities
    for (int i = 0; i < CONFIG_HA_SWITCH_ENTITY_NUM; i++) {
        ha_switch_entities[i] = (ha_switch_entity_t){.index = i, .key = switch_keys[i], .topic_set = switch_topics_set[i], .topic_state = switch_topics_state[i], .qos = CONFIG_TOPIC_SWITCH_QOS};
    }
}

static void ha_ctrl_cfg_restore(void)
{
    size_t    len = sizeof(switch_state);
    esp_err_t ret = indicator_nvs_read(HA_CFG_STORAGE, switch_state, &len);

    if (ret == ESP_OK && len == sizeof(switch_state)) {
        ESP_LOGI(TAG, "ctrl config read successfully");
    } else {
        ESP_LOGW(TAG, "read control config failed, initializing to defaults");
        memset(switch_state, 0, sizeof(switch_state));
    }
}

static void ha_ctrl_cfg_save(void)
{
    esp_err_t ret = indicator_nvs_write(HA_CFG_STORAGE, switch_state, sizeof(switch_state));
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "ctrl cfg write err:%d", ret);
    } else {
        ESP_LOGI(TAG, "ctrl cfg write successful");
    }
}

static int mqtt_msg_handler(const char* p_topic, int topic_len, const char* p_data, int data_len)
{
    cJSON* root = cJSON_ParseWithLength(p_data, data_len);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON data");
        return -1;
    }
    // struct view_data_ha_sensor_data sensor_data;
    // struct view_data_ha_switch_data switch_data;

    // memset(&sensor_data, 0, sizeof(sensor_data));
    // memset(&switch_data, 0, sizeof(switch_data));

    // for(int i = 0; i < CONFIG_HA_SENSOR_ENTITY_NUM; i++)
    // {
    // 	cjson_item = cJSON_GetObjectItem(root, ha_sensor_entities[i].key);
    // 	if(cjson_item != NULL && cjson_item->valuestring != NULL &&
    // 	   0 == strncmp(p_topic, ha_sensor_entities->topic, topic_len))
    // 	{
    // 		sensor_data.index = i;
    // 		strncpy(sensor_data.value, cjson_item->valuestring, sizeof(sensor_data.value) - 1);
    // esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SENSOR, &sensor_data,  sizeof(sensor_data), portMAX_DELAY);
    // 		// return 0;
    // 	}
    // }

    for (int i = 0; i < CONFIG_HA_SWITCH_ENTITY_NUM; i++) {
        if (strncmp(p_topic, ha_switch_entities[i].topic_set, topic_len) == 0) {  // The corresponding Topic then takes action
            cJSON* cjson_item = cJSON_GetObjectItem(root, ha_switch_entities[i].key);
            if (cjson_item != NULL && cJSON_IsNumber(cjson_item)) {
                struct view_data_ha_switch_data switch_data = {.index = i, .value = cjson_item->valueint};
                ESP_LOGI(TAG, "MQTT message: switch %d set to %d", i, switch_data.value);
                esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SWITCH_SET, &switch_data, sizeof(switch_data), portMAX_DELAY);
                cJSON_Delete(root);
                return 0;
            }
        }
    }

    ESP_LOGW(TAG, "No matching topic or invalid JSON structure");
    cJSON_Delete(root);
    return -1;
}

static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data)
{
    esp_mqtt_event_handle_t  event  = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int                      msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:

            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            instance_ptr->mqtt_connected_flag = true;

            for (int i = 0; i < CONFIG_HA_SENSOR_ENTITY_NUM; i++) {
                msg_id = esp_mqtt_client_subscribe(client, ha_sensor_entities[i].topic, ha_sensor_entities[i].qos);
                ESP_LOGI(TAG, "subscribe:%s, msg_id=%d", ha_sensor_entities[i].topic, msg_id);
            }

            for (int i = 0; i < CONFIG_HA_SWITCH_ENTITY_NUM; i++) {
                msg_id = esp_mqtt_client_subscribe(client, ha_switch_entities[i].topic_set, ha_switch_entities[i].qos);
                ESP_LOGI(TAG, "subscribe:%s, msg_id=%d", ha_switch_entities[i].topic_set, msg_id);
            }

            if (restore_task_handle == NULL) {
                xTaskCreate(restore_data_task, "restore_data_task", 4096, NULL, 5, &restore_task_handle);
            }

            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            instance_ptr->mqtt_connected_flag = false;

            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            // msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0,
            // 0); ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            printf("MQTT_EVENT_DATA: TOPIC=%.*s\r\n", event->topic_len, event->topic);
            printf("MQTT_EVENT_DATA: DATA=%.*s\r\n", event->data_len, event->data);
            mqtt_msg_handler(event->topic, event->topic_len, event->data, event->data_len);
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

static void publish_sensor_data(const struct view_data_sensor_data* sensor_data)
{
    if (!instance_ptr->mqtt_connected_flag) {
        return;
    }

    char        data_buf[MAX_DATA_BUF_LEN];
    int         len   = 0;
    const char* topic = CONFIG_SENSOR_BUILDIN_TOPIC_DATA;
    const char* key   = NULL;

    switch (sensor_data->sensor_type) {
        case SCD41_SENSOR_CO2:
            key = CONFIG_SENSOR_BUILDIN_CO2_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%d\"}", key, (int)sensor_data->value);
            break;
        case SGP40_SENSOR_TVOC:
            key = CONFIG_SENSOR_BUILDIN_TVOC_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%d\"}", key, (int)sensor_data->value);
            break;
        case SHT41_SENSOR_TEMP:
            key = CONFIG_SENSOR_BUILDIN_TEMP_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%.1f\"}", key, sensor_data->value);
            break;
        case SHT41_SENSOR_HUMIDITY:
            key = CONFIG_SENSOR_BUILDIN_HUMIDITY_VALUE_KEY;
            len = snprintf(data_buf, sizeof(data_buf), "{\"%s\":\"%d\"}", key, (int)sensor_data->value);
            break;
        default:
            ESP_LOGW(TAG, "Unknown sensor type: %d", sensor_data->sensor_type);
            return;
    }

    if (len > 0 && len < MAX_DATA_BUF_LEN) {
        esp_mqtt_client_publish(instance_ptr->mqtt_client, topic, data_buf, len, 0, 0);
    } else {
        ESP_LOGE(TAG, "Failed to format sensor data");
    }
}

static void publish_switch_state(const struct view_data_ha_switch_data* switch_data)
{
    if (!instance_ptr->mqtt_connected_flag) {
        return;
    }

    static const char* switch_keys[]   = CONFIG_SWITCH_VALUE_KEYS;
    static const char* switch_topics[] = CONFIG_SWITCH_TOPICS_STATE;

    if (switch_data->index >= 0 && switch_data->index < CONFIG_HA_SWITCH_ENTITY_NUM) {
        char data_buf[MAX_DATA_BUF_LEN];
        int  len = snprintf(data_buf, sizeof(data_buf), "{\"%s\": %d}", switch_keys[switch_data->index], (int)switch_data->value);

        if (len > 0 && len < MAX_DATA_BUF_LEN) {
            esp_mqtt_client_publish(instance_ptr->mqtt_client, switch_topics[switch_data->index], data_buf, len, 0, 0);

            if (switch_data->index < CONFIG_HA_SWITCH_ENTITY_NUM) {
                switch_state[switch_data->index] = switch_data->value;
                ha_ctrl_cfg_save();  // save switch state to flash
            }
        } else {
            ESP_LOGE(TAG, "Failed to format switch data");
        }
    } else {
        ESP_LOGE(TAG, "Invalid switch index: %d", switch_data->index);
    }
}

static void handle_wifi_status_change(const struct view_data_wifi_st* wifi_status)
{
    ESP_LOGI(TAG, "WiFi status changed. Connected: %d", wifi_status->is_network);
    if (wifi_status->is_network && instance_ptr->is_using) {
        esp_event_post_to(mqtt_app_event_handle, MQTT_APP_EVENT_BASE, MQTT_APP_START, &instance_ptr, sizeof(instance_mqtt_t), portMAX_DELAY);
    } else {
        // TODO: Implement MQTT shutdown logic if needed
    }
}

static void __view_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    switch (id) {
        case VIEW_EVENT_SENSOR_DATA:
            ESP_LOGI(TAG, "event: VIEW_EVENT_SENSOR_DATA");
            publish_sensor_data((struct view_data_sensor_data*)event_data);
            break;

        case VIEW_EVENT_HA_SWITCH_ST:
            ESP_LOGI(TAG, "event: VIEW_EVENT_HA_SWITCH_ST");
            publish_switch_state((struct view_data_ha_switch_data*)event_data);  // broadcast: my state.
            break;

        case VIEW_EVENT_WIFI_ST:
            ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_ST");
            handle_wifi_status_change((struct view_data_wifi_st*)event_data);
            break;

        default:
            ESP_LOGW(TAG, "Unhandled event id: %ld", id);
            break;
    }
}

/****************** MQTT client init ******************/
static void __cfg_event_handler(void* handler_args, esp_event_base_t base, int32_t id, void* event_data)
{
    ha_cfg_interface* p_cfg = NULL;
    switch (id) {
        case HA_CFG_SET:
            ESP_LOGI(TAG, "event: HA_CFG_BROKER_SET");
            
            esp_event_post_to(mqtt_app_event_handle, MQTT_APP_EVENT_BASE, MQTT_APP_RESTART, &instance_ptr, sizeof(instance_mqtt_t), portMAX_DELAY);
            break;
        case HA_CFG_BROKER_CHANGED: {
            if (!event_data)
                break;
            const char* broker_url = (const char*)event_data;
            ESP_LOGI(TAG, "HA_CFG_BROKER_CHANGED: %s", broker_url);
            esp_err_t ret = esp_mqtt_client_stop(instance_ptr->mqtt_client);
            ret           = esp_mqtt_client_set_uri(instance_ptr->mqtt_client, broker_url);
            ret           = esp_mqtt_client_start(instance_ptr->mqtt_client);
            // ESP_LOGI(TAG, "Broker address changed, reconnecting MQTT client");
            // esp_event_post_to(mqtt_app_event_handle,
            //                   MQTT_APP_EVENT_BASE,
            //                   MQTT_APP_RESTART,
            //                   &instance_ptr,
            //                   sizeof(instance_mqtt_t),
            //                   portMAX_DELAY);
            break;
        }
        default:
            break;
    }
}

/**
 * @brief
 * @attention excuted by `instance->mqtt_starter` on mqtt.c
 * @param instance
 */
static void _mqtt_ha_start(instance_mqtt* instance)
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

    // read from nvs
    ha_cfg_interface hf_cfg;
    if (ha_cfg_get(&hf_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get HA configuration");
        return;
    }

    instance->mqtt_cfg  = (esp_mqtt_client_config_t*)malloc(sizeof(esp_mqtt_client_config_t));
    *instance->mqtt_cfg = (esp_mqtt_client_config_t){
        .broker.address.uri                  = hf_cfg.broker_url,
        .credentials.client_id               = hf_cfg.client_id,
        .credentials.username                = hf_cfg.username,
        .credentials.authentication.password = hf_cfg.password,
    };

    ESP_LOGI(TAG, "| Broker Address               | %-40s |", hf_cfg.broker_url);
    ESP_LOGI(TAG, "| Client ID                    | %-40s |", hf_cfg.client_id);
    ESP_LOGI(TAG, "| username                     | %-40s |", hf_cfg.username);
    ESP_LOGI(TAG, "| password                     | %-40s |", hf_cfg.password);

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

/****************** Public Function Definitions ******************/
/**
 * @brief restore the configuration of a broker
 * to restore the broker address used last time
 *
 */
esp_err_t ha_cfg_get(ha_cfg_interface* ha_cfg)
{
    int len = sizeof(ha_cfg_interface);
    memset(ha_cfg, 0, sizeof(ha_cfg_interface));
    esp_err_t err = indicator_nvs_read(MQTT_HA_CFG_STORAGE, ha_cfg, &len);
    if (err == ESP_OK && len == sizeof(ha_cfg_interface)) {
        ESP_LOGI(TAG, "mqtt broker cfg read successful");
    } else {
        // err or not find
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "mqtt broker cfg not find");
        } else {
            ESP_LOGI(TAG, "mqtt broker cfg read err:%d", err);
        }
        // Using default settings
        strlcpy(ha_cfg->broker_url, CONFIG_BROKER_URL, sizeof(ha_cfg->broker_url));
        strlcpy(ha_cfg->client_id, CONFIG_MQTT_CLIENT_ID, sizeof(ha_cfg->client_id));
        strlcpy(ha_cfg->username, CONFIG_MQTT_USERNAME, sizeof(ha_cfg->username));
        strlcpy(ha_cfg->password, CONFIG_MQTT_PASSWORD, sizeof(ha_cfg->password));
    }
    return err;
}

/**
 * @brief store the configuration of a broker
 * store the broker address that will be used next time when device restart
 *
 */
esp_err_t ha_cfg_set(ha_cfg_interface* cfg)
{
    esp_err_t err = indicator_nvs_write(MQTT_HA_CFG_STORAGE, cfg, sizeof(ha_cfg_interface));
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "ha cfg write err:%d", err);
    } else {
        ESP_LOGI(TAG, "ha cfg write successful");
    }
    return err;
}

static void restore_data_task(void* args)
{
    ESP_LOGI(TAG, "Starting restore data task");

    for (int i = 0; i < CONFIG_HA_SWITCH_ENTITY_NUM; i++) {
        struct view_data_ha_switch_data switch_data = {.index = i, .value = switch_state[i]};
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SWITCH_SET, &switch_data, sizeof(switch_data), portMAX_DELAY);

        ESP_LOGI(TAG, "Restored switch %d state: %d", i, switch_state[i]);

        // Add a small delay to avoid sending events too quickly
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "Restore data task completed, deleting itself");
    restore_task_handle = NULL;
    vTaskDelete(NULL);
}

ESP_EVENT_DEFINE_BASE(HA_CFG_EVENT_BASE);
esp_event_loop_handle_t ha_cfg_event_handle;

int indicator_ha_model_init(void)
{
    ha_ctrl_cfg_restore(); /* restore the widets status last time */
    ha_entites_init(); /* define the entities, the ha data model */

    esp_event_loop_args_t ha_event_task_args = {.queue_size = 5, .task_name = "ha_event_task", .task_priority = uxTaskPriorityGet(NULL), .task_stack_size = 4096, .task_core_id = tskNO_AFFINITY};

    ESP_ERROR_CHECK(esp_event_loop_create(&ha_event_task_args, &ha_cfg_event_handle));

    /* start register mqtt service */
    ESP_LOGI(TAG, "mqtt_ha_init");

    mqtt_ha_instance = (instance_mqtt){.mqtt_name           = TAG,
                                       .mqtt_connected_flag = false, /* didn't connected, control by mqtt controller*/
                                       .mqtt_client         = NULL,  // client handler, use for start and stop
                                       .mqtt_cfg            = NULL,
                                       .mqtt_event_handler  = mqtt_event_handler,
                                       .mqtt_starter        = _mqtt_ha_start,
                                       .is_using            = true};

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(ha_cfg_event_handle, HA_CFG_EVENT_BASE, ESP_EVENT_ANY_ID, __cfg_event_handler, NULL, NULL)); /* 重启服务(会进行保存)，*/

    /* monitor network status*/
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_WIFI_ST, __view_event_handler, NULL, NULL));

    /* obtain sensor data */
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_SENSOR_DATA, __view_event_handler, NULL, NULL));
    /* get screen widgets status */
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SWITCH_ST, __view_event_handler, NULL, NULL));

    esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_ADDR_DISPLAY, NULL, 0, portMAX_DELAY);
}