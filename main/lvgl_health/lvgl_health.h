#ifndef LVGL_HEALTH_H
#define LVGL_HEALTH_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t lvgl_health_start(void);
void lvgl_health_heartbeat(void);

#ifdef __cplusplus
}
#endif

#endif
