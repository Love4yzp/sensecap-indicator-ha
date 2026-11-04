#ifndef HA_MQTT_H
#define HA_MQTT_H

#include "esp_event.h"
#include "mqtt.h"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(HA_CFG_EVENT_BASE);
extern esp_event_loop_handle_t ha_cfg_event_handle;
extern instance_mqtt           mqtt_ha_instance;

enum HA_CFG_EVENT {
    HA_CFG_SET,
    HA_CFG_BROKER_CHANGED,
    HA_CFG_EVENT_ALL,
};

esp_err_t ha_mqtt_set_enabled(bool enabled);
int indicator_ha_model_init(void);
int indicator_ha_view_init(void);

#ifdef __cplusplus
}
#endif

#endif /* HA_MQTT_H */
