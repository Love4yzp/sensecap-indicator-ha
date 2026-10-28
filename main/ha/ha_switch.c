#include "ha_switch.h"
#include "ha_mqtt.h"
#include "view_data.h"
#include "view_event.h"
#include "home_assistant_config.h"
#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"

#define MAX_DATA_BUF_LEN 64

static const char *TAG = "ha-switch";

typedef struct ha_switch_entity {
    int index;
    char *key;
    char *topic_set;
    char *topic_state;
    int qos;
} ha_switch_entity_t;

static ha_switch_entity_t ha_switch_entities[CONFIG_HA_SWITCH_ENTITY_NUM];
static int switch_state[CONFIG_HA_SWITCH_ENTITY_NUM];

static void publish_switch_result(
    const struct view_data_ha_switch_command *command,
    int authoritative_value,
    bool accepted)
{
    struct view_data_ha_switch_publish_result result = {
        .index = command->index,
        .value = authoritative_value,
        .generation = command->generation,
        .accepted = accepted,
    };
    view_event_delivery_t delivery = view_event_post_command(
        VIEW_EVENT_HA_SWITCH_PUBLISH_RESULT, &result, sizeof(result));
    if (delivery != VIEW_EVENT_DELIVERY_ENQUEUED) {
        ESP_LOGE(
            TAG,
            "switch publish result rejected: index=%u generation=%lu delivery=%d",
            (unsigned)command->index,
            (unsigned long)command->generation,
            (int)delivery);
    }
}

static void publish_switch_state(
    const struct view_data_ha_switch_command *command)
{
    bool accepted = false;
    int authoritative_value = command->value;

    static const char *switch_keys[] = CONFIG_SWITCH_VALUE_KEYS;
    static const char *switch_topics[] = CONFIG_SWITCH_TOPICS_STATE;

    if (command->index >= CONFIG_HA_SWITCH_ENTITY_NUM) {
        ESP_LOGE(TAG, "Invalid switch index: %u", (unsigned)command->index);
    } else {
        authoritative_value = switch_state[command->index];
        if (!mqtt_ha_instance.mqtt_connected_flag ||
            mqtt_ha_instance.mqtt_client == NULL) {
            ESP_LOGE(
                TAG,
                "switch publish rejected while MQTT is disconnected: index=%u",
                (unsigned)command->index);
        } else {
            char data_buf[MAX_DATA_BUF_LEN];
            int len = snprintf(
                data_buf,
                sizeof(data_buf),
                "{\"%s\": %d}",
                switch_keys[command->index],
                command->value);

            if (len > 0 && len < MAX_DATA_BUF_LEN) {
                int message_id = esp_mqtt_client_publish(
                    mqtt_ha_instance.mqtt_client,
                    switch_topics[command->index],
                    data_buf,
                    len,
                    0,
                    0);
                accepted = message_id >= 0;
                if (accepted) {
                    switch_state[command->index] = command->value;
                    authoritative_value = command->value;
                } else {
                    ESP_LOGE(
                        TAG,
                        "MQTT switch publish failed: index=%u result=%d",
                        (unsigned)command->index,
                        message_id);
                }
            } else {
                ESP_LOGE(TAG, "Failed to format switch data");
            }
        }
    }

    publish_switch_result(command, authoritative_value, accepted);
}

static void publish_initial_switch_state(void)
{
    for (int i = 0; i < CONFIG_HA_SWITCH_ENTITY_NUM; i++) {
        struct view_data_ha_switch_data switch_data = {.index = i, .value = switch_state[i]};
        view_event_delivery_t delivery = view_event_publish_state(
            VIEW_EVENT_HA_SWITCH_SET, &switch_data, sizeof(switch_data));
        if (delivery != VIEW_EVENT_DELIVERY_ENQUEUED &&
            delivery != VIEW_EVENT_DELIVERY_COALESCED) {
            ESP_LOGE(
                TAG,
                "initial switch %d dropped: delivery=%d",
                i,
                (int)delivery);
        }

        ESP_LOGI(TAG, "Initial switch %d state: %d", i, switch_state[i]);
    }
}

static void view_event_handler(
    void *handler_args, enum view_event_id id, const void *event_data)
{
    switch (id) {
        case VIEW_EVENT_HA_SWITCH_ST:
            ESP_LOGI(TAG, "event: VIEW_EVENT_HA_SWITCH_ST");
            publish_switch_state(
                (const struct view_data_ha_switch_command *)event_data);
            break;
        default:
            ESP_LOGW(TAG, "Unhandled event: %ld", id);
            break;
    }
}

void ha_switch_init(void)
{
    const char *switch_keys[] = CONFIG_SWITCH_VALUE_KEYS;
    const char *switch_topics_state[] = CONFIG_SWITCH_TOPICS_STATE;
    const char *switch_topics_set[] = CONFIG_SWITCH_TOPICS_SET;

    for (int i = 0; i < CONFIG_HA_SWITCH_ENTITY_NUM; i++) {
        ha_switch_entities[i] = (ha_switch_entity_t){
            .index = i,
            .key = (char *)switch_keys[i],
            .topic_set = (char *)switch_topics_set[i],
            .topic_state = (char *)switch_topics_state[i],
            .qos = CONFIG_TOPIC_SWITCH_QOS,
        };
    }

    publish_initial_switch_state();

    ESP_ERROR_CHECK(view_event_subscribe(
        VIEW_EVENT_HA_SWITCH_ST, view_event_handler, NULL) ? ESP_OK : ESP_FAIL);
}

void ha_switch_subscribe(esp_mqtt_client_handle_t client)
{
    for (int i = 0; i < CONFIG_HA_SWITCH_ENTITY_NUM; i++) {
        int msg_id = esp_mqtt_client_subscribe(client, ha_switch_entities[i].topic_set, ha_switch_entities[i].qos);
        ESP_LOGI(TAG, "subscribe:%s, msg_id=%d", ha_switch_entities[i].topic_set, msg_id);
    }
}

int ha_switch_on_mqtt_data(const char *topic, int topic_len, const char *data, int data_len)
{
    size_t expected_topic_len = strlen(ha_switch_entities[0].topic_set);
    if (topic_len != (int)expected_topic_len ||
        strncmp(topic, ha_switch_entities[0].topic_set, expected_topic_len) != 0) {
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

    for (int i = 0; i < CONFIG_HA_SWITCH_ENTITY_NUM; i++) {
        if (strncmp(c_key->string, ha_switch_entities[i].key, key_len) == 0) {
            cJSON *cjson_item = cJSON_GetObjectItem(root, ha_switch_entities[i].key);
            if (cjson_item != NULL && cJSON_IsNumber(cjson_item)) {
                struct view_data_ha_switch_data switch_data = {.index = i, .value = cjson_item->valueint};
                ESP_LOGI(TAG, "MQTT message: %s set to %d", ha_switch_entities[i].key, switch_data.value);
                switch_state[i] = switch_data.value;
                view_event_delivery_t delivery = view_event_publish_state(
                    VIEW_EVENT_HA_SWITCH_SET, &switch_data, sizeof(switch_data));
                if (delivery != VIEW_EVENT_DELIVERY_ENQUEUED &&
                    delivery != VIEW_EVENT_DELIVERY_COALESCED) {
                    ESP_LOGE(
                        TAG,
                        "MQTT switch update dropped: delivery=%d",
                        (int)delivery);
                    cJSON_Delete(root);
                    return -1;
                }
                cJSON_Delete(root);
                return 0;
            }
        }
    }

    ESP_LOGW(TAG, "No matching switch topic or invalid JSON structure");
    cJSON_Delete(root);
    return -1;
}
