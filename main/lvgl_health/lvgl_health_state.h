#ifndef LVGL_HEALTH_STATE_H
#define LVGL_HEALTH_STATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LVGL_HEALTH_STALL_WARNING_MS 3000U
#define LVGL_HEALTH_STALL_ESCALATION_MS 30000U

typedef enum {
    LVGL_HEALTH_ACTION_NONE = 0,
    LVGL_HEALTH_ACTION_STALLED,
    LVGL_HEALTH_ACTION_ESCALATED,
    LVGL_HEALTH_ACTION_RECOVERED,
} lvgl_health_action_t;

typedef struct {
    uint32_t last_sequence;
    uint64_t last_progress_ms;
    uint8_t stall_level;
} lvgl_health_state_t;

void lvgl_health_state_init(lvgl_health_state_t *state, uint64_t now_ms);

lvgl_health_action_t lvgl_health_state_observe(
    lvgl_health_state_t *state,
    uint64_t now_ms,
    uint32_t heartbeat_sequence);

#ifdef __cplusplus
}
#endif

#endif
