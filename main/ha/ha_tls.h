#ifndef HA_TLS_H
#define HA_TLS_H

#include <esp_err.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* TLS trust configuration for the MQTT connection (mqtts:// broker URLs).
 *
 * Stored OUTSIDE ha_cfg_interface on purpose: that struct's size is an NVS
 * compatibility contract (a size mismatch invalidates the stored broker
 * config), so TLS settings live under their own NVS keys and older/newer
 * firmware ignores them gracefully. */

#define HA_TLS_CA_MAX_LEN 4096

typedef enum {
    HA_TLS_MODE_VERIFY   = 0, /* verify server cert: custom CA if stored, else public bundle */
    HA_TLS_MODE_INSECURE = 1, /* skip server verification (explicit opt-in, discouraged) */
} ha_tls_mode_t;

ha_tls_mode_t ha_tls_mode_get(void);
esp_err_t     ha_tls_mode_set(ha_tls_mode_t mode);

/* Returns a malloc'd, NUL-terminated PEM (caller frees) or NULL when no CA is
 * stored. out_len (optional) receives the PEM length excluding the NUL. */
char     *ha_tls_ca_load(size_t *out_len);
esp_err_t ha_tls_ca_store(const char *pem, size_t len);
esp_err_t ha_tls_ca_clear(void);

/* True when url starts with the TLS scheme "mqtts://". */
bool ha_tls_url_is_tls(const char *url);

#ifdef __cplusplus
}
#endif

#endif /* HA_TLS_H */
