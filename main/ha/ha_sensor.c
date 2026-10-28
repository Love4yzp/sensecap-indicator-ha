#include "ha_sensor.h"
#include "ha_mqtt.h"
#include "view_data.h"
#include "view_event.h"
#include "home_assistant_config.h"
#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"

#define MAX_DATA_BUF_LEN 64

static const char *TAG = "ha-sensor";

typedef struct ha_sensor_entity {
    int index;
    char *key;
    char *topic;
    int qos;
} ha_sensor_entity_t;

static ha_sensor_entity_t ha_sensor_entities[CONFIG_HA_SENSOR_ENTITY_NUM];

static void publish_sensor_data(const struct view_data_sensor_data *sensor_data)
{
    if (!mqtt_ha_instance.mqtt_connected_flag) {
        return;
    }

    char data_buf[MAX_DATA_BUF_LEN];
    int len = 0;
    const char *topic = CONFIG_SENSOR_BUILDIN_TOPIC_DATA;
    const char *key = NULL;

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
        esp_mqtt_client_publish(mqtt_ha_instance.mqtt_client, topic, data_buf, len, 0, 0);
    } else {
        ESP_LOGE(TAG, "Failed to format sensor data");
    }
}

static void view_event_handler(
    void *handler_args, enum view_event_id id, const void *event_data)
{
    if (id == VIEW_EVENT_SENSOR_DATA) {
        ESP_LOGI(TAG, "event: VIEW_EVENT_SENSOR_DATA");
        publish_sensor_data((struct view_data_sensor_data *)event_data);
    }
}

void ha_sensor_init(void)
{
    const char *sensor_keys[] = CONFIG_SENSOR_VALUE_KEYS;
    const char *sensor_topics[] = CONFIG_SENSOR_TOPICS;

    for (int i = 0; i < CONFIG_HA_SENSOR_ENTITY_NUM; i++) {
        ha_sensor_entities[i] = (ha_sensor_entity_t){
            .index = i,
            .key = (char *)sensor_keys[i],
            .topic = (char *)sensor_topics[i],
            .qos = CONFIG_TOPIC_SENSOR_DATA_QOS,
        };
    }

    ESP_ERROR_CHECK(view_event_subscribe(
        VIEW_EVENT_SENSOR_DATA, view_event_handler, NULL) ? ESP_OK : ESP_FAIL);
}

void ha_sensor_subscribe(esp_mqtt_client_handle_t client)
{
    for (int i = 0; i < CONFIG_HA_SENSOR_ENTITY_NUM; i++) {
        int msg_id = esp_mqtt_client_subscribe(client, ha_sensor_entities[i].topic, ha_sensor_entities[i].qos);
        ESP_LOGI(TAG, "subscribe:%s, msg_id=%d", ha_sensor_entities[i].topic, msg_id);
    }
}

int ha_sensor_on_mqtt_data(const char *topic, int topic_len, const char *data, int data_len)
{
    if (strncmp(topic, ha_sensor_entities[0].topic, topic_len) != 0) {
        return -1;
    }

    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (root == NULL || root->child == NULL || root->child->string == NULL) {
        ESP_LOGE(TAG, "Invalid JSON structure");
        cJSON_Delete(root);
        return -1;
    }

    cJSON *c_key = root->child;
    ESP_LOGI(TAG, "c_key = %s", c_key->string);
    size_t key_len = strlen(c_key->string);

    for (int i = 0; i < CONFIG_HA_SENSOR_ENTITY_NUM; i++) {
        if (strncmp(c_key->string, ha_sensor_entities[i].key, key_len) == 0) {
            cJSON *cjson_item = cJSON_GetObjectItem(root, c_key->string);
            if (cjson_item != NULL && cJSON_IsString(cjson_item)) {
                struct view_data_ha_sensor_data sensor_data = {.index = i};
                strncpy(sensor_data.value, cjson_item->valuestring, sizeof(sensor_data.value) - 1);

                ESP_LOGI(TAG, "MQTT message: sensor %s is %s", ha_sensor_entities[i].key, sensor_data.value);
                /*
                 * NOTE: VIEW_EVENT_HA_SENSOR currently has NO consumer, so this
                 * value is parsed and posted but never shown. Wiring it up turns
                 * the Indicator into a generic HA dashboard: display ANY entity
                 * pushed from Home Assistant over MQTT, with no physical sensor
                 * attached to the device (the use case in issue #5).
                 *
                 * To wire it up:
                 *   1. ha_switch_screen.c::_create_sensor_card already builds the
                 *      "N/A" data labels on NAV_TILE_HA_MIX but discards the handle.
                 *      Store each label keyed by card index (cf. the switch_slot_t
                 *      pattern).
                 *   2. Register a VIEW_EVENT_HA_SENSOR handler that, under the LVGL
                 *      lock, does lv_label_set_text(labels[data->index], data->value).
                 *   3. Make sensor_data.index here map to the intended card slot.
                 */
                view_event_publish_state(
                    VIEW_EVENT_HA_SENSOR, &sensor_data, sizeof(sensor_data));
                cJSON_Delete(root);
                return 0;
            }
        }
    }

    ESP_LOGW(TAG, "No matching sensor topic or invalid JSON structure");
    cJSON_Delete(root);
    return -1;
}
