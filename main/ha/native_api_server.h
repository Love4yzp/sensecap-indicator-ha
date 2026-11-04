#ifndef NATIVE_API_SERVER_H
#define NATIVE_API_SERVER_H

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVE_API_DEFAULT_PORT 6053

esp_err_t native_api_server_init(void);
void native_api_server_set_enabled(bool enabled);
void native_api_server_set_has_ip(bool has_ip);
void native_api_server_configuration_changed(void);
void native_api_server_get_status(bool *running, bool *has_ip, uint8_t *client_count);

#ifdef __cplusplus
}
#endif

#endif /* NATIVE_API_SERVER_H */
