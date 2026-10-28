#include "ha_switch_sync.h"

#include <stddef.h>

static ha_switch_sync_action_t no_action(void)
{
    return (ha_switch_sync_action_t){
        .kind = HA_SWITCH_SYNC_ACTION_NONE,
    };
}

static uint32_t next_generation(ha_switch_sync_t *state)
{
    state->next_generation++;
    if (state->next_generation == 0) {
        state->next_generation++;
    }
    return state->next_generation;
}

void ha_switch_sync_init(ha_switch_sync_t *state, int initial_value)
{
    if (state == NULL) {
        return;
    }

    *state = (ha_switch_sync_t){
        .visible_value = initial_value,
        .accepted_value = initial_value,
    };
}

ha_switch_sync_action_t ha_switch_sync_user_changed(
    ha_switch_sync_t *state, int value, ha_switch_sync_mode_t mode)
{
    if (state == NULL) {
        return no_action();
    }

    uint32_t generation = next_generation(state);
    state->visible_generation = generation;
    state->visible_value = value;
    state->debounce_pending = mode == HA_SWITCH_SYNC_DEBOUNCED;
    state->result_timed_out = false;

    return (ha_switch_sync_action_t){
        .kind = state->debounce_pending
                    ? HA_SWITCH_SYNC_ACTION_SCHEDULE
                    : HA_SWITCH_SYNC_ACTION_PUBLISH,
        .generation = generation,
        .value = value,
    };
}

ha_switch_sync_action_t ha_switch_sync_debounce_elapsed(
    ha_switch_sync_t *state)
{
    if (state == NULL || !state->debounce_pending) {
        return no_action();
    }

    state->debounce_pending = false;
    return (ha_switch_sync_action_t){
        .kind = HA_SWITCH_SYNC_ACTION_PUBLISH,
        .generation = state->visible_generation,
        .value = state->visible_value,
    };
}

ha_switch_sync_action_t ha_switch_sync_publish_result(
    ha_switch_sync_t *state,
    uint32_t generation,
    int authoritative_value,
    bool accepted)
{
    if (state == NULL || generation == 0) {
        return no_action();
    }

    bool reapply_after_timeout =
        accepted &&
        generation == state->visible_generation &&
        state->result_timed_out &&
        state->timed_out_generation == generation;
    bool reapply_after_newer_local_rejection =
        accepted && generation > state->visible_generation;

    if (accepted && generation > state->accepted_generation) {
        state->accepted_generation = generation;
        state->accepted_value = authoritative_value;
    }

    if (generation != state->visible_generation &&
        !reapply_after_newer_local_rejection) {
        return no_action();
    }

    state->result_timed_out = false;
    if (accepted) {
        if (!reapply_after_timeout &&
            !reapply_after_newer_local_rejection) {
            return no_action();
        }
        state->visible_value = state->accepted_value;
        state->visible_generation = state->accepted_generation;
        return (ha_switch_sync_action_t){
            .kind = HA_SWITCH_SYNC_ACTION_APPLY,
            .generation = state->accepted_generation,
            .value = state->accepted_value,
        };
    }

    state->accepted_value = authoritative_value;
    state->visible_generation = state->accepted_generation;
    state->visible_value = state->accepted_value;
    state->debounce_pending = false;
    return (ha_switch_sync_action_t){
        .kind = HA_SWITCH_SYNC_ACTION_APPLY,
        .generation = state->accepted_generation,
        .value = state->accepted_value,
    };
}

ha_switch_sync_action_t ha_switch_sync_local_rejected(
    ha_switch_sync_t *state, uint32_t generation)
{
    if (state == NULL) {
        return no_action();
    }
    return ha_switch_sync_publish_result(
        state, generation, state->accepted_value, false);
}

ha_switch_sync_action_t ha_switch_sync_result_timeout(
    ha_switch_sync_t *state, uint32_t generation)
{
    if (state == NULL || generation != state->visible_generation) {
        return no_action();
    }

    state->result_timed_out = true;
    state->timed_out_generation = generation;
    state->visible_value = state->accepted_value;
    return (ha_switch_sync_action_t){
        .kind = HA_SWITCH_SYNC_ACTION_APPLY,
        .generation = generation,
        .value = state->accepted_value,
    };
}

ha_switch_sync_action_t ha_switch_sync_model_changed(
    ha_switch_sync_t *state, int value)
{
    if (state == NULL) {
        return no_action();
    }

    uint32_t generation = next_generation(state);
    state->visible_generation = generation;
    state->accepted_generation = generation;
    state->visible_value = value;
    state->accepted_value = value;
    state->debounce_pending = false;
    state->result_timed_out = false;

    return (ha_switch_sync_action_t){
        .kind = HA_SWITCH_SYNC_ACTION_CANCEL_AND_APPLY,
        .generation = generation,
        .value = value,
    };
}
