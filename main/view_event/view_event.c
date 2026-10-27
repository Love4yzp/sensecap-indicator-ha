#include "view_event.h"

#include "view_event_adapter.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef ESP_PLATFORM
#include "esp_log.h"
#define VIEW_EVENT_LOG_ERROR(...) ESP_LOGE("view-event", __VA_ARGS__)
#else
#define VIEW_EVENT_LOG_ERROR(...) ((void)0)
#endif

#define VIEW_EVENT_MAX_SUBSCRIBERS 32
#define VIEW_EVENT_MAX_STATE_SLOTS 24

struct view_event_subscription {
    enum view_event_id id;
    view_event_handler_t handler;
    void *context;
};

struct view_event_state_slot {
    bool used;
    enum view_event_id id;
    uint32_t key;
    size_t payload_size;
    union view_event_payload_storage payload;
};

static struct view_event_subscription s_subscriptions[VIEW_EVENT_MAX_SUBSCRIBERS];
static size_t s_subscription_count;
static struct view_event_state_slot s_state_slots[VIEW_EVENT_MAX_STATE_SLOTS];
static struct view_event_stats s_stats;

static bool view_event_id_is_valid(enum view_event_id id)
{
    return id >= VIEW_EVENT_SCREEN_START && id < VIEW_EVENT_ALL;
}

static bool view_event_is_command(enum view_event_id id)
{
    switch (id) {
        case VIEW_EVENT_SCREEN_START:
        case VIEW_EVENT_WIFI_LIST:
        case VIEW_EVENT_WIFI_LIST_START:
        case VIEW_EVENT_WIFI_LIST_REQ:
        case VIEW_EVENT_WIFI_CONNECT:
        case VIEW_EVENT_WIFI_CONNECT_RET:
        case VIEW_EVENT_WIFI_CFG_DELETE:
        case VIEW_EVENT_TIME_CFG_UPDATE:
        case VIEW_EVENT_TIME_CFG_APPLY:
        case VIEW_EVENT_DISPLAY_CFG_APPLY:
        case VIEW_EVENT_SHUTDOWN:
        case VIEW_EVENT_FACTORY_RESET:
        case VIEW_EVENT_MQTT_ADDR_CHANGED:
        case VIEW_EVENT_HA_SWITCH_ST:
        case VIEW_EVENT_HA_SWITCH_SET:
            return true;
        default:
            return false;
    }
}

static bool view_event_is_state(enum view_event_id id)
{
    switch (id) {
        case VIEW_EVENT_TIME:
        case VIEW_EVENT_WIFI_ST:
        case VIEW_EVENT_CITY:
        case VIEW_EVENT_SENSOR_DATA:
        case VIEW_EVENT_SENSOR_TEMP_HISTORY:
        case VIEW_EVENT_SENSOR_HUMIDITY_HISTORY:
        case VIEW_EVENT_SENSOR_TVOC_HISTORY:
        case VIEW_EVENT_SENSOR_CO2_HISTORY:
        case VIEW_EVENT_SENSOR_DATA_HISTORY:
        case VIEW_EVENT_DISPLAY_CFG:
        case VIEW_EVENT_BRIGHTNESS_UPDATE:
        case VIEW_EVENT_SCREEN_CTRL:
        case VIEW_EVENT_HA_ADDR_DISPLAY:
        case VIEW_EVENT_HA_SENSOR:
            return true;
        default:
            return false;
    }
}

static size_t view_event_expected_payload_size(enum view_event_id id)
{
    switch (id) {
        case VIEW_EVENT_SCREEN_START:
            return sizeof(uint8_t);
        case VIEW_EVENT_TIME:
        case VIEW_EVENT_SCREEN_CTRL:
            return sizeof(bool);
        case VIEW_EVENT_WIFI_ST:
            return sizeof(struct view_data_wifi_st);
        case VIEW_EVENT_SENSOR_DATA:
            return sizeof(struct view_data_sensor_data);
        case VIEW_EVENT_SENSOR_DATA_HISTORY:
            return sizeof(struct view_data_sensor_history_data);
        case VIEW_EVENT_WIFI_LIST:
            return sizeof(struct view_data_wifi_list);
        case VIEW_EVENT_WIFI_CONNECT:
            return sizeof(struct view_data_wifi_config);
        case VIEW_EVENT_WIFI_CONNECT_RET:
            return sizeof(struct view_data_wifi_connet_ret_msg);
        case VIEW_EVENT_TIME_CFG_UPDATE:
        case VIEW_EVENT_TIME_CFG_APPLY:
            return sizeof(struct view_data_time_cfg);
        case VIEW_EVENT_DISPLAY_CFG:
        case VIEW_EVENT_DISPLAY_CFG_APPLY:
            return sizeof(struct view_data_display);
        case VIEW_EVENT_BRIGHTNESS_UPDATE:
            return sizeof(int32_t);
        case VIEW_EVENT_HA_SENSOR:
            return sizeof(struct view_data_ha_sensor_data);
        case VIEW_EVENT_HA_SWITCH_ST:
        case VIEW_EVENT_HA_SWITCH_SET:
            return sizeof(struct view_data_ha_switch_data);
        case VIEW_EVENT_CITY:
            return 32;
        case VIEW_EVENT_SENSOR_TEMP_HISTORY:
        case VIEW_EVENT_SENSOR_HUMIDITY_HISTORY:
        case VIEW_EVENT_SENSOR_TVOC_HISTORY:
        case VIEW_EVENT_SENSOR_CO2_HISTORY:
        case VIEW_EVENT_WIFI_LIST_START:
        case VIEW_EVENT_WIFI_LIST_REQ:
        case VIEW_EVENT_WIFI_CFG_DELETE:
        case VIEW_EVENT_SHUTDOWN:
        case VIEW_EVENT_FACTORY_RESET:
        case VIEW_EVENT_MQTT_ADDR_CHANGED:
        case VIEW_EVENT_HA_ADDR_DISPLAY:
            return 0;
        default:
            return SIZE_MAX;
    }
}

static bool view_event_payload_is_valid(
    enum view_event_id id, const void *payload, size_t payload_size)
{
    size_t expected = view_event_expected_payload_size(id);
    if (expected == SIZE_MAX || expected != payload_size) {
        return false;
    }
    return payload_size == 0 || payload != NULL;
}

static uint32_t view_event_state_key(
    enum view_event_id id, const void *payload)
{
    switch (id) {
        case VIEW_EVENT_SENSOR_DATA:
            return (uint32_t)((const struct view_data_sensor_data *)payload)->sensor_type;
        case VIEW_EVENT_HA_SENSOR:
            return (uint32_t)((const struct view_data_ha_sensor_data *)payload)->index;
        default:
            return 0;
    }
}

static void view_event_count_invalid(enum view_event_id id)
{
    (void)id;
    view_event_adapter_lock();
    s_stats.invalid_publications++;
    view_event_adapter_unlock();
    VIEW_EVENT_LOG_ERROR("invalid publication for event %d", (int)id);
}

static void view_event_dispatch(
    enum view_event_id id, const void *payload)
{
    struct view_event_subscription matches[VIEW_EVENT_MAX_SUBSCRIBERS];
    size_t match_count = 0;

    view_event_adapter_lock();
    for (size_t i = 0; i < s_subscription_count; i++) {
        if (s_subscriptions[i].id == id) {
            matches[match_count++] = s_subscriptions[i];
        }
    }
    view_event_adapter_unlock();

    for (size_t i = 0; i < match_count; i++) {
        matches[i].handler(matches[i].context, id, payload);
    }
}

bool view_event_init(void)
{
    if (!view_event_adapter_init()) {
        return false;
    }

    view_event_adapter_lock();
    memset(s_subscriptions, 0, sizeof(s_subscriptions));
    memset(s_state_slots, 0, sizeof(s_state_slots));
    memset(&s_stats, 0, sizeof(s_stats));
    s_subscription_count = 0;
    view_event_adapter_unlock();
    return true;
}

bool view_event_subscribe(
    enum view_event_id id, view_event_handler_t handler, void *context)
{
    if (!view_event_id_is_valid(id) || handler == NULL) {
        return false;
    }

    bool subscribed = false;
    view_event_adapter_lock();
    if (s_subscription_count < VIEW_EVENT_MAX_SUBSCRIBERS) {
        s_subscriptions[s_subscription_count++] = (struct view_event_subscription){
            .id = id,
            .handler = handler,
            .context = context,
        };
        subscribed = true;
    }
    view_event_adapter_unlock();
    return subscribed;
}

view_event_delivery_t view_event_post_command(
    enum view_event_id id, const void *payload, size_t payload_size)
{
    if (!view_event_is_command(id) ||
        !view_event_payload_is_valid(id, payload, payload_size) ||
        payload_size > VIEW_EVENT_MAX_PAYLOAD_SIZE) {
        view_event_count_invalid(id);
        return VIEW_EVENT_DELIVERY_INVALID;
    }

    view_event_adapter_result_t result =
        view_event_adapter_post_command(id, payload, payload_size);

    view_event_adapter_lock();
    if (result == VIEW_EVENT_ADAPTER_OK) {
        s_stats.command_enqueued++;
    } else {
        s_stats.command_rejected++;
    }
    view_event_adapter_unlock();

    if (result != VIEW_EVENT_ADAPTER_OK) {
        VIEW_EVENT_LOG_ERROR("critical command %d rejected by delivery adapter", (int)id);
        return VIEW_EVENT_DELIVERY_REJECTED;
    }
    return VIEW_EVENT_DELIVERY_ENQUEUED;
}

view_event_delivery_t view_event_publish_state(
    enum view_event_id id, const void *payload, size_t payload_size)
{
    if (!view_event_is_state(id) ||
        !view_event_payload_is_valid(id, payload, payload_size) ||
        payload_size > VIEW_EVENT_MAX_PAYLOAD_SIZE) {
        view_event_count_invalid(id);
        return VIEW_EVENT_DELIVERY_INVALID;
    }

    uint32_t key = view_event_state_key(id, payload);
    size_t slot = VIEW_EVENT_MAX_STATE_SLOTS;
    bool coalesced = false;

    view_event_adapter_lock();
    for (size_t i = 0; i < VIEW_EVENT_MAX_STATE_SLOTS; i++) {
        if (s_state_slots[i].used &&
            s_state_slots[i].id == id &&
            s_state_slots[i].key == key) {
            slot = i;
            coalesced = true;
            break;
        }
        if (!s_state_slots[i].used && slot == VIEW_EVENT_MAX_STATE_SLOTS) {
            slot = i;
        }
    }

    if (slot < VIEW_EVENT_MAX_STATE_SLOTS) {
        struct view_event_state_slot *state = &s_state_slots[slot];
        state->used = true;
        state->id = id;
        state->key = key;
        state->payload_size = payload_size;
        if (payload_size > 0) {
            memcpy(state->payload.bytes, payload, payload_size);
        }
        if (coalesced) {
            s_stats.state_coalesced++;
        }
    }
    if (slot == VIEW_EVENT_MAX_STATE_SLOTS) {
        s_stats.state_dropped++;
        view_event_adapter_unlock();
        VIEW_EVENT_LOG_ERROR("no state slot available for event %d", (int)id);
        return VIEW_EVENT_DELIVERY_DROPPED;
    }

    if (coalesced) {
        view_event_adapter_unlock();
        return VIEW_EVENT_DELIVERY_COALESCED;
    }

    view_event_adapter_result_t result = view_event_adapter_post_state(slot);
    if (result == VIEW_EVENT_ADAPTER_OK) {
        s_stats.state_enqueued++;
    } else {
        memset(&s_state_slots[slot], 0, sizeof(s_state_slots[slot]));
        s_stats.state_dropped++;
    }
    view_event_adapter_unlock();

    if (result != VIEW_EVENT_ADAPTER_OK) {
        VIEW_EVENT_LOG_ERROR("replaceable state %d dropped by delivery adapter", (int)id);
        return VIEW_EVENT_DELIVERY_DROPPED;
    }
    return VIEW_EVENT_DELIVERY_ENQUEUED;
}

void view_event_stats_get(struct view_event_stats *stats)
{
    if (stats == NULL) {
        return;
    }
    view_event_adapter_lock();
    *stats = s_stats;
    view_event_adapter_unlock();
}

void view_event_internal_dispatch_command(
    enum view_event_id id, const void *payload)
{
    view_event_adapter_lock();
    s_stats.command_dispatched++;
    view_event_adapter_unlock();
    view_event_dispatch(id, payload);
}

void view_event_internal_dispatch_state(size_t slot)
{
    if (slot >= VIEW_EVENT_MAX_STATE_SLOTS) {
        return;
    }

    struct view_event_state_slot delivered;
    bool available = false;

    view_event_adapter_lock();
    if (s_state_slots[slot].used) {
        delivered = s_state_slots[slot];
        memset(&s_state_slots[slot], 0, sizeof(s_state_slots[slot]));
        s_stats.state_dispatched++;
        available = true;
    }
    view_event_adapter_unlock();

    if (available) {
        const void *payload =
            delivered.payload_size > 0 ? delivered.payload.bytes : NULL;
        view_event_dispatch(delivered.id, payload);
    }
}
