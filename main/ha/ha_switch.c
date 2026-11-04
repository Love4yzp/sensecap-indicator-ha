#include "ha_switch.h"
#include "entity_core.h"
#include "entity_registry.h"
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
            "switch result rejected: index=%u generation=%lu delivery=%d",
            (unsigned)command->index,
            (unsigned long)command->generation,
            (int)delivery);
    }
}

static void publish_mqtt_state(uint8_t control_slot, int value)
{
    char data_buf[MAX_DATA_BUF_LEN];
    int len = snprintf(
        data_buf,
        sizeof(data_buf),
        "{\"%s\": %d}",
        ha_switch_entities[control_slot].key,
        value);
    if (len <= 0 || len >= MAX_DATA_BUF_LEN) {
        ESP_LOGE(TAG, "Failed to format switch data");
        return;
    }

    esp_err_t result = mqtt_publish_enqueue(
        &mqtt_ha_instance,
        ha_switch_entities[control_slot].topic_state,
        data_buf,
        (size_t)len);
    if (result != ESP_OK) {
        ESP_LOGW(
            TAG,
            "MQTT switch state dropped: index=%u result=%d",
            (unsigned)control_slot,
            result);
    }
}

static void core_state_observer(
    const entity_core_snapshot_t *snapshot, void *context)
{
    (void)context;
    if (snapshot->index < ENTITY_INDEX_CONTROL_FIRST ||
        snapshot->index >= ENTITY_INDEX_COUNT || !snapshot->valid) {
        return;
    }

    uint8_t control_slot =
        (uint8_t)(snapshot->index - ENTITY_INDEX_CONTROL_FIRST);
    int value = (int)snapshot->value;
    struct view_data_ha_switch_data switch_data = {
        .index = control_slot,
        .value = value,
    };
    view_event_delivery_t delivery = view_event_publish_state(
        VIEW_EVENT_HA_SWITCH_SET, &switch_data, sizeof(switch_data));
    if (delivery != VIEW_EVENT_DELIVERY_ENQUEUED &&
        delivery != VIEW_EVENT_DELIVERY_COALESCED) {
        ESP_LOGE(
            TAG,
            "switch state update dropped: index=%u delivery=%d",
            (unsigned)control_slot,
            (int)delivery);
    }

    publish_mqtt_state(control_slot, value);
}

static void publish_switch_command(
    const struct view_data_ha_switch_command *command)
{
    uint8_t entity_index = entity_registry_control_index(command->index);
    if (entity_index == ENTITY_INDEX_INVALID) {
        ESP_LOGE(TAG, "Invalid switch index: %u", (unsigned)command->index);
        publish_switch_result(command, command->value, false);
        return;
    }

    entity_core_command_t core_command = {
        .index = entity_index,
        .value = (float)command->value,
        .generation = command->generation,
        .source = ENTITY_CORE_SOURCE_LOCAL_UI,
    };
    entity_core_command_result_t core_result;
    entity_core_status_t status =
        entity_core_submit_command(&core_command, &core_result);
    if (status != ENTITY_CORE_STATUS_OK) {
        ESP_LOGE(
            TAG,
            "switch command rejected: index=%u status=%d",
            (unsigned)command->index,
            (int)status);
    }
    publish_switch_result(
        command, (int)core_result.value, core_result.accepted);
}

static void publish_current_switch_states(void)
{
    if (!entity_core_replay(core_state_observer, NULL)) {
        ESP_LOGE(TAG, "Failed to replay switch snapshots");
    }
}

static void view_event_handler(
    void *handler_args, enum view_event_id id, const void *event_data)
{
    switch (id) {
        case VIEW_EVENT_HA_SWITCH_ST:
            ESP_LOGI(TAG, "event: VIEW_EVENT_HA_SWITCH_ST");
            publish_switch_command(
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

    ESP_ERROR_CHECK(
        entity_core_add_observer(core_state_observer, NULL) ? ESP_OK : ESP_FAIL);
    publish_current_switch_states();

    ESP_ERROR_CHECK(view_event_subscribe(
        VIEW_EVENT_HA_SWITCH_ST, view_event_handler, NULL) ? ESP_OK : ESP_FAIL);
}

void ha_switch_subscribe(esp_mqtt_client_handle_t client)
{
    for (int i = 0; i < CONFIG_HA_SWITCH_ENTITY_NUM; i++) {
        int msg_id = esp_mqtt_client_subscribe(client, ha_switch_entities[i].topic_set, ha_switch_entities[i].qos);
        ESP_LOGI(TAG, "subscribe:%s, msg_id=%d", ha_switch_entities[i].topic_set, msg_id);
    }
    publish_current_switch_states();
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
    for (int i = 0; i < CONFIG_HA_SWITCH_ENTITY_NUM; i++) {
        if (strcmp(c_key->string, ha_switch_entities[i].key) != 0) {
            continue;
        }

        cJSON *cjson_item =
            cJSON_GetObjectItemCaseSensitive(root, ha_switch_entities[i].key);
        if (cjson_item == NULL || !cJSON_IsNumber(cjson_item)) {
            break;
        }

        entity_core_command_t command = {
            .index = entity_registry_control_index((uint8_t)i),
            .value = (float)cjson_item->valuedouble,
            .source = ENTITY_CORE_SOURCE_MQTT,
        };
        entity_core_command_result_t result;
        entity_core_status_t status = entity_core_submit_command(&command, &result);
        if (status != ENTITY_CORE_STATUS_OK) {
            ESP_LOGW(
                TAG,
                "MQTT switch command rejected: index=%d status=%d",
                i,
                (int)status);
            cJSON_Delete(root);
            return -1;
        }

        ESP_LOGI(
            TAG,
            "MQTT message: %s set to %d",
            ha_switch_entities[i].key,
            (int)result.value);
        cJSON_Delete(root);
        return 0;
    }

    ESP_LOGW(TAG, "No matching switch topic or invalid JSON structure");
    cJSON_Delete(root);
    return -1;
}
