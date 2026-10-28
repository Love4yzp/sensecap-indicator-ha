#include "lvgl_health_state.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>

enum {
    LVGL_HEALTH_STALL_LEVEL_NONE = 0,
    LVGL_HEALTH_STALL_LEVEL_WARNED,
    LVGL_HEALTH_STALL_LEVEL_ESCALATED,
};

void lvgl_health_state_init(lvgl_health_state_t *state, uint64_t now_ms)
{
    assert(state != NULL);
    *state = (lvgl_health_state_t){
        .last_progress_ms = now_ms,
    };
}

lvgl_health_action_t lvgl_health_state_observe(
    lvgl_health_state_t *state,
    uint64_t now_ms,
    uint32_t heartbeat_sequence)
{
    assert(state != NULL);

    if (heartbeat_sequence != state->last_sequence) {
        const bool recovered =
            state->stall_level != LVGL_HEALTH_STALL_LEVEL_NONE;
        state->last_sequence = heartbeat_sequence;
        state->last_progress_ms = now_ms;
        state->stall_level = LVGL_HEALTH_STALL_LEVEL_NONE;
        return recovered ?
            LVGL_HEALTH_ACTION_RECOVERED :
            LVGL_HEALTH_ACTION_NONE;
    }

    const uint64_t stalled_ms = now_ms - state->last_progress_ms;
    if (stalled_ms >= LVGL_HEALTH_STALL_ESCALATION_MS &&
        state->stall_level == LVGL_HEALTH_STALL_LEVEL_WARNED) {
        state->stall_level = LVGL_HEALTH_STALL_LEVEL_ESCALATED;
        return LVGL_HEALTH_ACTION_ESCALATED;
    }
    if (stalled_ms >= LVGL_HEALTH_STALL_WARNING_MS &&
        state->stall_level == LVGL_HEALTH_STALL_LEVEL_NONE) {
        state->stall_level = LVGL_HEALTH_STALL_LEVEL_WARNED;
        return LVGL_HEALTH_ACTION_STALLED;
    }

    return LVGL_HEALTH_ACTION_NONE;
}
