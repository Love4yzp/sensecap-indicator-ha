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
#define VIEW_EVENT_COMMAND_ATTEMPTS 3
#define VIEW_EVENT_STATE_LOCK_WAIT_MS 0
#define VIEW_EVENT_INTERNAL_LOCK_WAIT_MS 5

enum view_event_category {
    VIEW_EVENT_CATEGORY_INVALID = 0,
    VIEW_EVENT_CATEGORY_COMMAND,
    VIEW_EVENT_CATEGORY_STATE,
};

enum view_event_key_kind {
    VIEW_EVENT_KEY_SINGLETON = 0,
    VIEW_EVENT_KEY_SENSOR_TYPE,
    VIEW_EVENT_KEY_HA_SENSOR_INDEX,
};

struct view_event_descriptor {
    enum view_event_category category;
    size_t payload_size;
    enum view_event_key_kind key_kind;
};

#define COMMAND(payload_type) \
    { VIEW_EVENT_CATEGORY_COMMAND, sizeof(payload_type), VIEW_EVENT_KEY_SINGLETON }
#define COMMAND_EMPTY \
    { VIEW_EVENT_CATEGORY_COMMAND, 0, VIEW_EVENT_KEY_SINGLETON }
#define STATE(payload_type) \
    { VIEW_EVENT_CATEGORY_STATE, sizeof(payload_type), VIEW_EVENT_KEY_SINGLETON }
#define STATE_KEYED(payload_type, key) \
    { VIEW_EVENT_CATEGORY_STATE, sizeof(payload_type), key }
#define STATE_EMPTY \
    { VIEW_EVENT_CATEGORY_STATE, 0, VIEW_EVENT_KEY_SINGLETON }

static const struct view_event_descriptor s_descriptors[VIEW_EVENT_ALL] = {
    [VIEW_EVENT_SCREEN_START] = COMMAND(uint8_t),
    [VIEW_EVENT_TIME] = STATE(bool),
    [VIEW_EVENT_WIFI_ST] = STATE(struct view_data_wifi_st),
    [VIEW_EVENT_CITY] = {
        VIEW_EVENT_CATEGORY_STATE, 32, VIEW_EVENT_KEY_SINGLETON},
    [VIEW_EVENT_SENSOR_DATA] = STATE_KEYED(
        struct view_data_sensor_data, VIEW_EVENT_KEY_SENSOR_TYPE),
    [VIEW_EVENT_SENSOR_TEMP_HISTORY] = STATE_EMPTY,
    [VIEW_EVENT_SENSOR_HUMIDITY_HISTORY] = STATE_EMPTY,
    [VIEW_EVENT_SENSOR_TVOC_HISTORY] = STATE_EMPTY,
    [VIEW_EVENT_SENSOR_CO2_HISTORY] = STATE_EMPTY,
    [VIEW_EVENT_SENSOR_DATA_HISTORY] =
        STATE(struct view_data_sensor_history_data),
    [VIEW_EVENT_WIFI_LIST] = COMMAND(struct view_data_wifi_list),
    [VIEW_EVENT_WIFI_LIST_START] = COMMAND_EMPTY,
    [VIEW_EVENT_WIFI_LIST_REQ] = COMMAND_EMPTY,
    [VIEW_EVENT_WIFI_CONNECT] = COMMAND(struct view_data_wifi_config),
    [VIEW_EVENT_WIFI_CONNECT_RET] =
        COMMAND(struct view_data_wifi_connet_ret_msg),
    [VIEW_EVENT_WIFI_CFG_DELETE] = COMMAND_EMPTY,
    [VIEW_EVENT_TIME_CFG_UPDATE] = COMMAND(struct view_data_time_cfg),
    [VIEW_EVENT_TIME_CFG_APPLY] = COMMAND(struct view_data_time_cfg),
    [VIEW_EVENT_DISPLAY_CFG] = STATE(struct view_data_display),
    [VIEW_EVENT_BRIGHTNESS_UPDATE] = STATE(int32_t),
    [VIEW_EVENT_DISPLAY_CFG_APPLY] = COMMAND(struct view_data_display),
    [VIEW_EVENT_SHUTDOWN] = COMMAND_EMPTY,
    [VIEW_EVENT_FACTORY_RESET] = COMMAND_EMPTY,
    [VIEW_EVENT_SCREEN_CTRL] = STATE(bool),
    [VIEW_EVENT_MQTT_ADDR_CHANGED] = COMMAND_EMPTY,
    [VIEW_EVENT_HA_ADDR_DISPLAY] = STATE_EMPTY,
    [VIEW_EVENT_HA_SENSOR] = STATE_KEYED(
        struct view_data_ha_sensor_data, VIEW_EVENT_KEY_HA_SENSOR_INDEX),
    [VIEW_EVENT_HA_SWITCH_ST] =
        COMMAND(struct view_data_ha_switch_data),
    [VIEW_EVENT_HA_SWITCH_SET] =
        COMMAND(struct view_data_ha_switch_data),
};

#undef COMMAND
#undef COMMAND_EMPTY
#undef STATE
#undef STATE_KEYED
#undef STATE_EMPTY

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

#define STATS_INCREMENT(field) \
    ((void)__atomic_add_fetch(&s_stats.field, 1U, __ATOMIC_RELAXED))
#define STATS_LOAD(field) \
    __atomic_load_n(&s_stats.field, __ATOMIC_RELAXED)

static const struct view_event_descriptor *view_event_descriptor_for(
    enum view_event_id id)
{
    if (id < VIEW_EVENT_SCREEN_START || id >= VIEW_EVENT_ALL) {
        return NULL;
    }
    const struct view_event_descriptor *descriptor = &s_descriptors[id];
    if (descriptor->category == VIEW_EVENT_CATEGORY_INVALID) {
        return NULL;
    }
    return descriptor;
}

static bool view_event_payload_is_valid(
    const struct view_event_descriptor *descriptor,
    const void *payload,
    size_t payload_size)
{
    if (descriptor == NULL || descriptor->payload_size != payload_size) {
        return false;
    }
    return payload_size == 0 || payload != NULL;
}

static uint32_t view_event_state_key(
    const struct view_event_descriptor *descriptor, const void *payload)
{
    switch (descriptor->key_kind) {
        case VIEW_EVENT_KEY_SENSOR_TYPE:
            return (uint32_t)(
                (const struct view_data_sensor_data *)payload)->sensor_type;
        case VIEW_EVENT_KEY_HA_SENSOR_INDEX:
            return (uint32_t)(
                (const struct view_data_ha_sensor_data *)payload)->index;
        case VIEW_EVENT_KEY_SINGLETON:
        default:
            return 0;
    }
}

static void view_event_count_invalid(enum view_event_id id)
{
    (void)id;
    STATS_INCREMENT(invalid_publications);
    VIEW_EVENT_LOG_ERROR("invalid publication for event %d", (int)id);
}

static void view_event_copy_subscriptions(
    enum view_event_id id,
    struct view_event_subscription *matches,
    size_t *match_count)
{
    *match_count = 0;
    size_t subscription_count =
        __atomic_load_n(&s_subscription_count, __ATOMIC_ACQUIRE);
    for (size_t i = 0; i < subscription_count; i++) {
        if (s_subscriptions[i].id == id) {
            matches[(*match_count)++] = s_subscriptions[i];
        }
    }
}

static void view_event_dispatch(
    enum view_event_id id, const void *payload)
{
    struct view_event_subscription matches[VIEW_EVENT_MAX_SUBSCRIBERS];
    size_t match_count = 0;

    view_event_copy_subscriptions(id, matches, &match_count);
    for (size_t i = 0; i < match_count; i++) {
        matches[i].handler(matches[i].context, id, payload);
    }
}

bool view_event_init(void)
{
    if (!view_event_adapter_init() ||
        !view_event_adapter_lock(VIEW_EVENT_INTERNAL_LOCK_WAIT_MS)) {
        return false;
    }

    memset(s_subscriptions, 0, sizeof(s_subscriptions));
    memset(s_state_slots, 0, sizeof(s_state_slots));
    memset(&s_stats, 0, sizeof(s_stats));
    __atomic_store_n(&s_subscription_count, 0, __ATOMIC_RELEASE);
    view_event_adapter_unlock();
    return true;
}

bool view_event_subscribe(
    enum view_event_id id, view_event_handler_t handler, void *context)
{
    if (view_event_descriptor_for(id) == NULL || handler == NULL ||
        !view_event_adapter_lock(VIEW_EVENT_INTERNAL_LOCK_WAIT_MS)) {
        return false;
    }

    bool subscribed = false;
    size_t subscription_count =
        __atomic_load_n(&s_subscription_count, __ATOMIC_RELAXED);
    if (subscription_count < VIEW_EVENT_MAX_SUBSCRIBERS) {
        s_subscriptions[subscription_count] = (struct view_event_subscription){
            .id = id,
            .handler = handler,
            .context = context,
        };
        __atomic_store_n(
            &s_subscription_count, subscription_count + 1, __ATOMIC_RELEASE);
        subscribed = true;
    }
    view_event_adapter_unlock();
    return subscribed;
}

view_event_delivery_t view_event_post_command(
    enum view_event_id id, const void *payload, size_t payload_size)
{
    const struct view_event_descriptor *descriptor =
        view_event_descriptor_for(id);
    if (descriptor == NULL ||
        descriptor->category != VIEW_EVENT_CATEGORY_COMMAND ||
        !view_event_payload_is_valid(descriptor, payload, payload_size) ||
        payload_size > VIEW_EVENT_MAX_PAYLOAD_SIZE) {
        view_event_count_invalid(id);
        return VIEW_EVENT_DELIVERY_INVALID;
    }

    for (unsigned attempt = 0; attempt < VIEW_EVENT_COMMAND_ATTEMPTS; attempt++) {
        view_event_adapter_result_t result =
            view_event_adapter_post_command(id, payload, payload_size);
        if (result == VIEW_EVENT_ADAPTER_OK) {
            STATS_INCREMENT(command_enqueued);
            return VIEW_EVENT_DELIVERY_ENQUEUED;
        }
        if (attempt + 1 < VIEW_EVENT_COMMAND_ATTEMPTS) {
            STATS_INCREMENT(command_retries);
        }
    }

    STATS_INCREMENT(command_rejected);
    VIEW_EVENT_LOG_ERROR(
        "critical command %d rejected after %u bounded attempts",
        (int)id,
        VIEW_EVENT_COMMAND_ATTEMPTS);
    return VIEW_EVENT_DELIVERY_REJECTED;
}

view_event_delivery_t view_event_publish_state(
    enum view_event_id id, const void *payload, size_t payload_size)
{
    const struct view_event_descriptor *descriptor =
        view_event_descriptor_for(id);
    if (descriptor == NULL ||
        descriptor->category != VIEW_EVENT_CATEGORY_STATE ||
        !view_event_payload_is_valid(descriptor, payload, payload_size) ||
        payload_size > VIEW_EVENT_MAX_PAYLOAD_SIZE) {
        view_event_count_invalid(id);
        return VIEW_EVENT_DELIVERY_INVALID;
    }

    if (!view_event_adapter_lock(VIEW_EVENT_STATE_LOCK_WAIT_MS)) {
        STATS_INCREMENT(synchronization_busy);
        STATS_INCREMENT(state_dropped);
        VIEW_EVENT_LOG_ERROR(
            "replaceable state %d dropped: synchronization busy", (int)id);
        return VIEW_EVENT_DELIVERY_DROPPED;
    }

    uint32_t key = view_event_state_key(descriptor, payload);
    size_t slot = VIEW_EVENT_MAX_STATE_SLOTS;
    bool coalesced = false;
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

    if (slot == VIEW_EVENT_MAX_STATE_SLOTS) {
        view_event_adapter_unlock();
        STATS_INCREMENT(state_dropped);
        VIEW_EVENT_LOG_ERROR("no state slot available for event %d", (int)id);
        return VIEW_EVENT_DELIVERY_DROPPED;
    }

    struct view_event_state_slot *state = &s_state_slots[slot];
    state->used = true;
    state->id = id;
    state->key = key;
    state->payload_size = payload_size;
    if (payload_size > 0) {
        memcpy(state->payload.bytes, payload, payload_size);
    }
    if (coalesced) {
        view_event_adapter_unlock();
        STATS_INCREMENT(state_coalesced);
        return VIEW_EVENT_DELIVERY_COALESCED;
    }

    view_event_adapter_result_t result = view_event_adapter_post_state(slot);
    if (result != VIEW_EVENT_ADAPTER_OK) {
        memset(state, 0, sizeof(*state));
    }
    view_event_adapter_unlock();

    if (result == VIEW_EVENT_ADAPTER_OK) {
        STATS_INCREMENT(state_enqueued);
        return VIEW_EVENT_DELIVERY_ENQUEUED;
    }

    STATS_INCREMENT(state_dropped);
    VIEW_EVENT_LOG_ERROR(
        "replaceable state %d dropped by delivery adapter", (int)id);
    return VIEW_EVENT_DELIVERY_DROPPED;
}

void view_event_stats_get(struct view_event_stats *stats)
{
    if (stats == NULL) {
        return;
    }
    *stats = (struct view_event_stats){
        .command_enqueued = STATS_LOAD(command_enqueued),
        .command_retries = STATS_LOAD(command_retries),
        .command_rejected = STATS_LOAD(command_rejected),
        .command_dispatched = STATS_LOAD(command_dispatched),
        .state_enqueued = STATS_LOAD(state_enqueued),
        .state_coalesced = STATS_LOAD(state_coalesced),
        .state_dropped = STATS_LOAD(state_dropped),
        .state_dispatched = STATS_LOAD(state_dispatched),
        .synchronization_busy = STATS_LOAD(synchronization_busy),
        .invalid_publications = STATS_LOAD(invalid_publications),
    };
}

void view_event_internal_dispatch_command(
    enum view_event_id id, const void *payload)
{
    view_event_dispatch(id, payload);
    STATS_INCREMENT(command_dispatched);
}

bool view_event_internal_dispatch_state(size_t slot)
{
    if (slot >= VIEW_EVENT_MAX_STATE_SLOTS) {
        return false;
    }
    if (!view_event_adapter_lock(VIEW_EVENT_INTERNAL_LOCK_WAIT_MS)) {
        STATS_INCREMENT(synchronization_busy);
        view_event_adapter_defer_state(slot);
        return false;
    }

    struct view_event_state_slot delivered;
    bool available = false;
    if (s_state_slots[slot].used) {
        delivered = s_state_slots[slot];
        memset(&s_state_slots[slot], 0, sizeof(s_state_slots[slot]));
        available = true;
    }
    view_event_adapter_unlock();

    if (available) {
        const void *payload =
            delivered.payload_size > 0 ? delivered.payload.bytes : NULL;
        view_event_dispatch(delivered.id, payload);
        STATS_INCREMENT(state_dispatched);
    }
    return true;
}
