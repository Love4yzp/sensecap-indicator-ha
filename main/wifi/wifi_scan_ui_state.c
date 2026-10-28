#include "wifi_scan_ui_state.h"

#include <stddef.h>

uint32_t wifi_scan_ui_begin(struct wifi_scan_ui_state *state)
{
    if (state == NULL) {
        return 0;
    }

    state->next_generation++;
    if (state->next_generation == 0) {
        state->next_generation++;
    }
    state->active_generation = state->next_generation;
    state->pending = true;
    state->accept_result = true;
    return state->active_generation;
}

void wifi_scan_ui_hide(struct wifi_scan_ui_state *state)
{
    if (state != NULL && state->pending) {
        state->accept_result = false;
    }
}

enum wifi_scan_ui_completion wifi_scan_ui_complete(
    struct wifi_scan_ui_state *state, uint32_t generation)
{
    if (state == NULL || !state->pending ||
        generation != state->active_generation) {
        return WIFI_SCAN_UI_COMPLETION_STALE;
    }

    state->pending = false;
    return state->accept_result
        ? WIFI_SCAN_UI_COMPLETION_CURRENT
        : WIFI_SCAN_UI_COMPLETION_DISCARD;
}

bool wifi_scan_ui_timeout(
    struct wifi_scan_ui_state *state, uint32_t generation)
{
    return wifi_scan_ui_complete(state, generation) !=
        WIFI_SCAN_UI_COMPLETION_STALE;
}
