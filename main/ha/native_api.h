#ifndef NATIVE_API_H
#define NATIVE_API_H

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool enabled;
    bool has_ip;
    bool psk_configured;
    bool running;
    uint8_t client_count;
} native_api_runtime_status_t;

esp_err_t native_api_init(void);
esp_err_t native_api_set_enabled(bool enabled);
void native_api_configuration_changed(void);
void native_api_get_status(native_api_runtime_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* NATIVE_API_H */
