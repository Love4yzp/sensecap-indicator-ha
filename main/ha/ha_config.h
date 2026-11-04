#ifndef HA_CONFIG_H
#define HA_CONFIG_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MQTT_HA_CFG_STORAGE "ha-mqtt"
#define MQTT_HA_ENABLED_STORAGE "mqtt-enabled"

typedef struct {
    char broker_url[128];
    char client_id[16];
    char username[32];
    char password[64];
} ha_cfg_interface;

bool      ha_cfg_is_valid(const ha_cfg_interface *cfg);
esp_err_t ha_cfg_get_stored(ha_cfg_interface *cfg);
esp_err_t ha_cfg_get(ha_cfg_interface *cfg);
esp_err_t ha_cfg_set(ha_cfg_interface *cfg);
bool      ha_mqtt_enabled_get(void);
esp_err_t ha_mqtt_enabled_set(bool enabled);
void      ha_config_view_init(void);

#ifdef __cplusplus
}
#endif

#endif /* HA_CONFIG_H */
