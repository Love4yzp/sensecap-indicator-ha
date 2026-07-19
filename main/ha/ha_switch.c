#include "ha_switch.h"
#include "ha_mqtt.h"
#include "view_data.h"
#include "home_assistant_config.h"
#include "cJSON.h"
#include "storage_nvs.h"
#include "lv_port.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define HA_CFG_STORAGE "ha-cfg"
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
/* Snapshot of what is currently persisted in NVS, for the save dirty-check. */
static int persisted_switch_state[CONFIG_HA_SWITCH_ENTITY_NUM];
static bool persisted_state_valid = false;
static TaskHandle_t restore_task_handle = NULL;
static ha_switch_screen_t *_screen = NULL;

static void ha_ctrl_cfg_restore(void)
{
    size_t len = sizeof(switch_state);
    esp_err_t ret = indicator_nvs_read(HA_CFG_STORAGE, switch_state, &len);

    if (ret == ESP_OK && len == sizeof(switch_state)) {
        ESP_LOGI(TAG, "ctrl config read successfully");
    } else {
        ESP_LOGW(TAG, "read control config failed, initializing to defaults");
        memset(switch_state, 0, sizeof(switch_state));
    }

    /* Seed the dirty-check snapshot so an identical save is skipped. */
    memcpy(persisted_switch_state, switch_state, sizeof(switch_state));
    persisted_state_valid = true;
}

static void ha_ctrl_cfg_save(void)
{
    /*
     * Dirty-check: skip the flash write when nothing changed. This absorbs the
     * per-tick save flood during slider/arc drags and the full 8-switch
     * re-apply the restore task performs on every MQTT reconnect.
     */
    if (persisted_state_valid &&
        memcmp(persisted_switch_state, switch_state, sizeof(switch_state)) == 0) {
        return;
    }

    esp_err_t ret = indicator_nvs_write(HA_CFG_STORAGE, switch_state, sizeof(switch_state));
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "ctrl cfg write err:%d", ret);
    } else {
        memcpy(persisted_switch_state, switch_state, sizeof(switch_state));
        persisted_state_valid = true;
        ESP_LOGI(TAG, "ctrl cfg write successful");
    }
}

static void publish_switch_state(const struct view_data_ha_switch_data *switch_data)
{
    if (!mqtt_ha_instance.mqtt_connected_flag) {
        return;
    }

    static const char *switch_keys[] = CONFIG_SWITCH_VALUE_KEYS;
    static const char *switch_topics[] = CONFIG_SWITCH_TOPICS_STATE;

    if (switch_data->index >= 0 && switch_data->index < CONFIG_HA_SWITCH_ENTITY_NUM) {
        char data_buf[MAX_DATA_BUF_LEN];
        int len = snprintf(data_buf, sizeof(data_buf), "{\"%s\": %d}", switch_keys[switch_data->index], (int)switch_data->value);

        if (len > 0 && len < MAX_DATA_BUF_LEN) {
            esp_mqtt_client_publish(mqtt_ha_instance.mqtt_client, switch_topics[switch_data->index], data_buf, len, 0, 0);
            switch_state[switch_data->index] = switch_data->value;
            ha_ctrl_cfg_save();
        } else {
            ESP_LOGE(TAG, "Failed to format switch data");
        }
    } else {
        ESP_LOGE(TAG, "Invalid switch index: %d", switch_data->index);
    }
}

void ha_switch_attach_screen(ha_switch_screen_t *screen)
{
    _screen = screen;
}

static void restore_data_task(void *args)
{
    ESP_LOGI(TAG, "Starting restore data task");

    for (int i = 0; i < CONFIG_HA_SWITCH_ENTITY_NUM; i++) {
        struct view_data_ha_switch_data switch_data = {.index = i, .value = switch_state[i]};
        esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SWITCH_SET, &switch_data, sizeof(switch_data), portMAX_DELAY);

        ESP_LOGI(TAG, "Restored switch %d state: %d", i, switch_state[i]);

        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG, "Restore data task completed, deleting itself");
    restore_task_handle = NULL;
    vTaskDelete(NULL);
}

static void view_event_handler(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    switch (id) {
        case VIEW_EVENT_HA_SWITCH_ST:
            ESP_LOGI(TAG, "event: VIEW_EVENT_HA_SWITCH_ST");
            publish_switch_state((struct view_data_ha_switch_data *)event_data);
            break;
        case VIEW_EVENT_HA_SWITCH_SET: {
            struct view_data_ha_switch_data *p = (struct view_data_ha_switch_data *)event_data;
            ESP_LOGI(TAG, "VIEW_EVENT_HA_SWITCH_SET: switch index:%d value:%d\n", p->index, p->value);
            if (_screen) {
                lv_port_sem_take();
                ha_switch_screen_update(_screen, p->index, p->value);
                lv_port_sem_give();
            }
            /*
             * The screen update no longer bounces back through LVGL into
             * VIEW_EVENT_HA_SWITCH_ST, so emit the protocol-required state echo
             * on indicator/switch/state and persist here -- exactly the work
             * the ST handler does for user-driven changes, done once, in this
             * task's own context (no LVGL round-trip, no self-post).
             */
            publish_switch_state(p);
            break;
        }
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

    ha_ctrl_cfg_restore();

    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SWITCH_ST, view_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register_with(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SWITCH_SET, view_event_handler, NULL, NULL));
}

void ha_switch_subscribe(esp_mqtt_client_handle_t client)
{
    for (int i = 0; i < CONFIG_HA_SWITCH_ENTITY_NUM; i++) {
        int msg_id = esp_mqtt_client_subscribe(client, ha_switch_entities[i].topic_set, ha_switch_entities[i].qos);
        ESP_LOGI(TAG, "subscribe:%s, msg_id=%d", ha_switch_entities[i].topic_set, msg_id);
    }

    if (restore_task_handle == NULL) {
        xTaskCreate(restore_data_task, "restore_data_task", 4096, NULL, 5, &restore_task_handle);
    }
}

int ha_switch_on_mqtt_data(const char *topic, int topic_len, const char *data, int data_len)
{
    if (strncmp(topic, ha_switch_entities[0].topic_set, topic_len) != 0) {
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
                esp_event_post_to(view_event_handle, VIEW_EVENT_BASE, VIEW_EVENT_HA_SWITCH_SET, &switch_data, sizeof(switch_data), portMAX_DELAY);
                cJSON_Delete(root);
                return 0;
            }
        }
    }

    ESP_LOGW(TAG, "No matching switch topic or invalid JSON structure");
    cJSON_Delete(root);
    return -1;
}
