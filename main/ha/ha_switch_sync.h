#ifndef HA_SWITCH_SYNC_H
#define HA_SWITCH_SYNC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HA_SWITCH_SYNC_IMMEDIATE = 0,
    HA_SWITCH_SYNC_DEBOUNCED,
} ha_switch_sync_mode_t;

typedef enum {
    HA_SWITCH_SYNC_ACTION_NONE = 0,
    HA_SWITCH_SYNC_ACTION_SCHEDULE,
    HA_SWITCH_SYNC_ACTION_PUBLISH,
    HA_SWITCH_SYNC_ACTION_APPLY,
    HA_SWITCH_SYNC_ACTION_CANCEL_AND_APPLY,
} ha_switch_sync_action_kind_t;

typedef struct {
    ha_switch_sync_action_kind_t kind;
    uint32_t generation;
    int value;
} ha_switch_sync_action_t;

typedef struct {
    uint32_t next_generation;
    uint32_t visible_generation;
    uint32_t accepted_generation;
    int visible_value;
    int accepted_value;
    bool debounce_pending;
    bool result_timed_out;
    uint32_t timed_out_generation;
} ha_switch_sync_t;

void ha_switch_sync_init(ha_switch_sync_t *state, int initial_value);

ha_switch_sync_action_t ha_switch_sync_user_changed(
    ha_switch_sync_t *state, int value, ha_switch_sync_mode_t mode);

ha_switch_sync_action_t ha_switch_sync_debounce_elapsed(
    ha_switch_sync_t *state);

ha_switch_sync_action_t ha_switch_sync_publish_result(
    ha_switch_sync_t *state,
    uint32_t generation,
    int authoritative_value,
    bool accepted);

ha_switch_sync_action_t ha_switch_sync_local_rejected(
    ha_switch_sync_t *state, uint32_t generation);

ha_switch_sync_action_t ha_switch_sync_result_timeout(
    ha_switch_sync_t *state, uint32_t generation);

ha_switch_sync_action_t ha_switch_sync_model_changed(
    ha_switch_sync_t *state, int value);

#ifdef __cplusplus
}
#endif

#endif /* HA_SWITCH_SYNC_H */
