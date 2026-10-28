#include "button_press_state.h"

#define BUTTON_LONG_PRESS_START_MS 1500U
#define BUTTON_SLEEP_THRESHOLD_MS 3000U
#define BUTTON_FACTORY_RESET_THRESHOLD_MS 10000U
#define BUTTON_RELEASE_LIMIT_MS 15000U

static uint32_t s_elapsed_ms;
static bool s_sleep_prepare_emitted;
static bool s_restart_emitted;
static bool s_factory_reset_emitted;
static bool s_shutdown_emitted;

void button_press_begin(void)
{
    s_elapsed_ms = BUTTON_LONG_PRESS_START_MS;
    s_sleep_prepare_emitted = false;
    s_restart_emitted = false;
    s_factory_reset_emitted = false;
    s_shutdown_emitted = false;
}

button_press_action_t button_press_hold(uint32_t elapsed_delta_ms, bool sleeping)
{
    s_elapsed_ms += elapsed_delta_ms;

    if (s_elapsed_ms >= BUTTON_FACTORY_RESET_THRESHOLD_MS) {
        if (!s_factory_reset_emitted) {
            s_factory_reset_emitted = true;
            return BUTTON_PRESS_ACTION_FACTORY_RESET;
        }
        return BUTTON_PRESS_ACTION_NONE;
    }

    if (s_elapsed_ms >= BUTTON_SLEEP_THRESHOLD_MS) {
        if (sleeping) {
            if (!s_restart_emitted) {
                s_restart_emitted = true;
                return BUTTON_PRESS_ACTION_RESTART;
            }
        } else if (!s_sleep_prepare_emitted) {
            s_sleep_prepare_emitted = true;
            return BUTTON_PRESS_ACTION_PREPARE_SLEEP;
        }
    }

    return BUTTON_PRESS_ACTION_NONE;
}

button_press_action_t button_press_release(void)
{
    if (s_elapsed_ms < BUTTON_SLEEP_THRESHOLD_MS ||
        s_elapsed_ms >= BUTTON_RELEASE_LIMIT_MS ||
        s_factory_reset_emitted ||
        s_shutdown_emitted) {
        return BUTTON_PRESS_ACTION_NONE;
    }

    return BUTTON_PRESS_ACTION_SHUTDOWN;
}

void button_press_shutdown_accepted(void)
{
    s_shutdown_emitted = true;
}
