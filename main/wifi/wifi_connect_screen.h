#ifndef WIFI_CONNECT_SCREEN_H
#define WIFI_CONNECT_SCREEN_H

#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct wifi_connect_screen wifi_connect_screen_t;

/* Fired exactly once when the screen is torn down, from any dismiss path
 * (Cancel, Join, details Delete, or wifi_connect_screen_dismiss()). Lets the
 * owner drop its handle. Invoked from inside _dismiss() before the object is
 * freed; the callback MUST NOT free the screen or re-enter dismiss.           */
typedef void (*wifi_connect_dismiss_cb_t)(void *user_data);

/* Show a connect dialog for an unconnected AP (modal overlay).
 * Returns NULL if one is already open.                              */
wifi_connect_screen_t *wifi_connect_screen_show(const char *ssid, bool have_password);

/* Show a details dialog for the currently-connected AP (Delete/Cancel). */
wifi_connect_screen_t *wifi_details_screen_show(const char *ssid);

/* Register the one-shot dismiss notification. Safe to call with s == NULL
 * (e.g. when a show() returned NULL). Register right after show().            */
void wifi_connect_screen_set_dismiss_cb(wifi_connect_screen_t *s,
                                        wifi_connect_dismiss_cb_t on_dismiss,
                                        void *user_data);

/* Dismiss programmatically (safe to call if already dismissed). */
void wifi_connect_screen_dismiss(wifi_connect_screen_t *s);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_CONNECT_SCREEN_H */
