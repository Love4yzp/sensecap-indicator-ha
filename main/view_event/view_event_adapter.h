#ifndef VIEW_EVENT_ADAPTER_H
#define VIEW_EVENT_ADAPTER_H

#include "view_data_types.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Internal payload extent shared by the core and production adapter. Keep the
 * public interface semantic; transport storage remains behind this seam.
 */
union view_event_payload_extent {
    max_align_t alignment;
    uint8_t screen_start;
    bool boolean_value;
    char city[32];
    int32_t brightness;
    struct view_data_wifi_st wifi_state;
    struct view_data_wifi_config wifi_config;
    struct view_data_wifi_list wifi_list;
    struct view_data_wifi_connet_ret_msg wifi_connect_result;
    struct view_data_time_cfg time_config;
    struct view_data_display display_config;
    struct view_data_sensor_data sensor_data;
    struct view_data_sensor_history_data sensor_history;
    struct view_data_ha_sensor_data ha_sensor;
    struct view_data_ha_switch_data ha_switch;
};

#define VIEW_EVENT_MAX_PAYLOAD_SIZE sizeof(union view_event_payload_extent)
#define VIEW_EVENT_MAX_STATE_SLOTS 24

union view_event_payload_storage {
    max_align_t alignment;
    uint8_t bytes[VIEW_EVENT_MAX_PAYLOAD_SIZE];
};

typedef enum {
    VIEW_EVENT_ADAPTER_OK = 0,
    VIEW_EVENT_ADAPTER_FULL,
    VIEW_EVENT_ADAPTER_ERROR,
} view_event_adapter_result_t;

bool view_event_adapter_init(void);
view_event_adapter_result_t view_event_adapter_post_command(
    enum view_event_id id, const void *payload, size_t payload_size);
view_event_adapter_result_t view_event_adapter_post_state(size_t slot);
/*
 * Preserve a consumed state token outside the transport queue until internal
 * synchronization becomes available. This internal adapter operation must
 * not silently reject a valid slot.
 */
void view_event_adapter_defer_state(size_t slot);
bool view_event_adapter_lock(uint32_t timeout_ms);
void view_event_adapter_unlock(void);

/* Called only by delivery adapters after transport acceptance. */
void view_event_internal_dispatch_command(
    enum view_event_id id, const void *payload);
bool view_event_internal_dispatch_state(size_t slot);

#endif /* VIEW_EVENT_ADAPTER_H */
