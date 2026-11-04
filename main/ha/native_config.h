#ifndef NATIVE_CONFIG_H
#define NATIVE_CONFIG_H

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVE_CONFIG_PSK_SIZE 32
#define NATIVE_CONFIG_PSK_BASE64_LEN 44

typedef enum {
    NATIVE_CONFIG_PSK_NOT_CONFIGURED = 0,
    NATIVE_CONFIG_PSK_CONFIGURED,
    NATIVE_CONFIG_PSK_ERROR,
} native_config_psk_state_t;

bool native_config_enabled_get(void);
esp_err_t native_config_enabled_set(bool enabled);

esp_err_t native_config_psk_decode_base64(
    const char *encoded, uint8_t psk[NATIVE_CONFIG_PSK_SIZE]);
native_config_psk_state_t native_config_psk_read(
    uint8_t psk[NATIVE_CONFIG_PSK_SIZE]);
esp_err_t native_config_psk_set(const uint8_t psk[NATIVE_CONFIG_PSK_SIZE]);
esp_err_t native_config_psk_set_base64(const char *encoded);
esp_err_t native_config_psk_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* NATIVE_CONFIG_H */
