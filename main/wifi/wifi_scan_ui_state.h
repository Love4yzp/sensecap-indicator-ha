#ifndef WIFI_SCAN_UI_STATE_H
#define WIFI_SCAN_UI_STATE_H

#include <stdbool.h>
#include <stdint.h>

enum wifi_scan_ui_completion {
    WIFI_SCAN_UI_COMPLETION_STALE = 0,
    WIFI_SCAN_UI_COMPLETION_DISCARD,
    WIFI_SCAN_UI_COMPLETION_CURRENT,
};

struct wifi_scan_ui_state {
    uint32_t next_generation;
    uint32_t active_generation;
    bool pending;
    bool accept_result;
};

uint32_t wifi_scan_ui_begin(struct wifi_scan_ui_state *state);
void wifi_scan_ui_hide(struct wifi_scan_ui_state *state);
enum wifi_scan_ui_completion wifi_scan_ui_complete(
    struct wifi_scan_ui_state *state, uint32_t generation);
bool wifi_scan_ui_timeout(
    struct wifi_scan_ui_state *state, uint32_t generation);

#endif /* WIFI_SCAN_UI_STATE_H */
