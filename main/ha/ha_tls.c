#include "ha_tls.h"

#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "storage_nvs.h"

static const char *TAG = "ha-tls";

/* NVS keys (own entries, not part of the ha_cfg_interface blob — see header). */
#define TLS_MODE_STORAGE "mqtt-tls-mode"
#define TLS_CA_STORAGE   "mqtt-tls-ca"

/* A stored CA shorter than the PEM armor is treated as "none"; clearing writes
 * a single zero byte because the NVS wrapper has no erase-key operation. */
#define TLS_CA_MIN_PLAUSIBLE 32

ha_tls_mode_t ha_tls_mode_get(void)
{
    uint8_t mode = HA_TLS_MODE_VERIFY;
    size_t len = sizeof(mode);
    if (indicator_nvs_read(TLS_MODE_STORAGE, &mode, &len) != ESP_OK || len != sizeof(mode)) {
        return HA_TLS_MODE_VERIFY;
    }
    return (mode == HA_TLS_MODE_INSECURE) ? HA_TLS_MODE_INSECURE : HA_TLS_MODE_VERIFY;
}

esp_err_t ha_tls_mode_set(ha_tls_mode_t mode)
{
    uint8_t v = (uint8_t)mode;
    esp_err_t err = indicator_nvs_write(TLS_MODE_STORAGE, &v, sizeof(v));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to store TLS mode: %s", esp_err_to_name(err));
    }
    return err;
}

char *ha_tls_ca_load(size_t *out_len)
{
    /* Read into a max-size scratch first: the NVS wrapper needs a caller
     * buffer, and a stored blob larger than expected must not overflow. */
    char *buf = malloc(HA_TLS_CA_MAX_LEN + 1);
    if (buf == NULL) {
        return NULL;
    }
    size_t len = HA_TLS_CA_MAX_LEN;
    if (indicator_nvs_read(TLS_CA_STORAGE, buf, &len) != ESP_OK ||
        len < TLS_CA_MIN_PLAUSIBLE) {
        free(buf);
        return NULL;
    }
    buf[len] = '\0';
    if (out_len) {
        *out_len = len;
    }
    return buf;
}

esp_err_t ha_tls_ca_store(const char *pem, size_t len)
{
    if (pem == NULL || len < TLS_CA_MIN_PLAUSIBLE || len > HA_TLS_CA_MAX_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strstr(pem, "-----BEGIN CERTIFICATE-----") == NULL ||
        strstr(pem, "-----END CERTIFICATE-----") == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err = indicator_nvs_write(TLS_CA_STORAGE, (void *)pem, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to store CA (%u bytes): %s", (unsigned)len, esp_err_to_name(err));
    }
    return err;
}

esp_err_t ha_tls_ca_clear(void)
{
    uint8_t zero = 0;
    return indicator_nvs_write(TLS_CA_STORAGE, &zero, sizeof(zero));
}

bool ha_tls_url_is_tls(const char *url)
{
    return url != NULL && strncmp(url, "mqtts://", 8) == 0;
}
