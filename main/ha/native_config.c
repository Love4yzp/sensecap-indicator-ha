#include "native_config.h"

#include <string.h>

#include "storage_nvs.h"

#define NATIVE_ENABLED_STORAGE "native-enabled"
#define NATIVE_PSK_STORAGE "native-psk"

static int base64_value(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}

bool native_config_enabled_get(void)
{
    uint8_t enabled = 0;
    size_t len = sizeof(enabled);
    esp_err_t err = indicator_nvs_read(NATIVE_ENABLED_STORAGE, &enabled, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return true;
    }
    if (err != ESP_OK) {
        return false;
    }
    return len == sizeof(enabled) && enabled == 1;
}

esp_err_t native_config_enabled_set(bool enabled)
{
    uint8_t value = enabled ? 1 : 0;
    return indicator_nvs_write(NATIVE_ENABLED_STORAGE, &value, sizeof(value));
}

esp_err_t native_config_psk_decode_base64(
    const char *encoded, uint8_t psk[NATIVE_CONFIG_PSK_SIZE])
{
    if (encoded == NULL || psk == NULL ||
        strlen(encoded) != NATIVE_CONFIG_PSK_BASE64_LEN ||
        encoded[NATIVE_CONFIG_PSK_BASE64_LEN - 1] != '=') {
        return ESP_ERR_INVALID_ARG;
    }

    for (size_t i = 0; i < NATIVE_CONFIG_PSK_BASE64_LEN - 1; ++i) {
        if (base64_value(encoded[i]) < 0) {
            return ESP_ERR_INVALID_ARG;
        }
    }

    for (size_t i = 0, out = 0; i < 40; i += 4, out += 3) {
        uint8_t a = (uint8_t)base64_value(encoded[i]);
        uint8_t b = (uint8_t)base64_value(encoded[i + 1]);
        uint8_t c = (uint8_t)base64_value(encoded[i + 2]);
        uint8_t d = (uint8_t)base64_value(encoded[i + 3]);
        psk[out] = (uint8_t)((a << 2) | (b >> 4));
        psk[out + 1] = (uint8_t)((b << 4) | (c >> 2));
        psk[out + 2] = (uint8_t)((c << 6) | d);
    }

    uint8_t a = (uint8_t)base64_value(encoded[40]);
    uint8_t b = (uint8_t)base64_value(encoded[41]);
    uint8_t c = (uint8_t)base64_value(encoded[42]);
    if ((c & 0x03) != 0) {
        memset(psk, 0, NATIVE_CONFIG_PSK_SIZE);
        return ESP_ERR_INVALID_ARG;
    }
    psk[30] = (uint8_t)((a << 2) | (b >> 4));
    psk[31] = (uint8_t)((b << 4) | (c >> 2));
    uint8_t nonzero = 0;
    for (size_t i = 0; i < NATIVE_CONFIG_PSK_SIZE; ++i) {
        nonzero |= psk[i];
    }
    if (nonzero == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

native_config_psk_state_t native_config_psk_read(
    uint8_t psk[NATIVE_CONFIG_PSK_SIZE])
{
    if (psk == NULL) {
        return NATIVE_CONFIG_PSK_ERROR;
    }

    uint8_t stored[NATIVE_CONFIG_PSK_SIZE] = {0};
    size_t len = sizeof(stored);
    memset(psk, 0, NATIVE_CONFIG_PSK_SIZE);
    esp_err_t err = indicator_nvs_read(NATIVE_PSK_STORAGE, stored, &len);
    native_config_psk_state_t state = NATIVE_CONFIG_PSK_ERROR;
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        state = NATIVE_CONFIG_PSK_NOT_CONFIGURED;
    } else if (err == ESP_OK && len == 1 && stored[0] == 0) {
        state = NATIVE_CONFIG_PSK_NOT_CONFIGURED;
    } else if (err == ESP_OK && len == sizeof(stored)) {
        uint8_t nonzero = 0;
        for (size_t i = 0; i < sizeof(stored); ++i) {
            nonzero |= stored[i];
        }
        if (nonzero != 0) {
            memcpy(psk, stored, sizeof(stored));
            state = NATIVE_CONFIG_PSK_CONFIGURED;
        }
    }
    memset(stored, 0, sizeof(stored));
    return state;
}

esp_err_t native_config_psk_set(const uint8_t psk[NATIVE_CONFIG_PSK_SIZE])
{
    if (psk == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint8_t nonzero = 0;
    for (size_t i = 0; i < NATIVE_CONFIG_PSK_SIZE; ++i) {
        nonzero |= psk[i];
    }
    if (nonzero == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return indicator_nvs_write(
        NATIVE_PSK_STORAGE, (void *)psk, NATIVE_CONFIG_PSK_SIZE);
}

esp_err_t native_config_psk_set_base64(const char *encoded)
{
    uint8_t psk[NATIVE_CONFIG_PSK_SIZE] = {0};
    esp_err_t err = native_config_psk_decode_base64(encoded, psk);
    if (err == ESP_OK) {
        err = native_config_psk_set(psk);
    }
    memset(psk, 0, sizeof(psk));
    return err;
}

esp_err_t native_config_psk_clear(void)
{
    uint8_t empty = 0;
    return indicator_nvs_write(NATIVE_PSK_STORAGE, &empty, sizeof(empty));
}
