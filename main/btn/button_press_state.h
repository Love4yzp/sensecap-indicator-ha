#ifndef BUTTON_PRESS_STATE_H
#define BUTTON_PRESS_STATE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BUTTON_PRESS_ACTION_NONE = 0,
    BUTTON_PRESS_ACTION_PREPARE_SLEEP,
    BUTTON_PRESS_ACTION_RESTART,
    BUTTON_PRESS_ACTION_FACTORY_RESET,
    BUTTON_PRESS_ACTION_SHUTDOWN,
} button_press_action_t;

void button_press_begin(void);
button_press_action_t button_press_hold(uint32_t elapsed_delta_ms, bool sleeping);
button_press_action_t button_press_release(void);
uint32_t button_press_elapsed_ms(void);

#endif /* BUTTON_PRESS_STATE_H */
