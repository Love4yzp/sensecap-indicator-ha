/**
 * @file indicator_ha.h
 * @date  23 July 2024

 * @author Spencer Yan
 *
 * @note
 *
 * @copyright © 2024, Seeed Studio
 */

#ifndef INDICATOR_HA_H
#define INDICATOR_HA_H

#include "view_data.h"

#define MQTT_HA_CFG_STORAGE "ha-mqtt"

#ifdef __cplusplus
extern "C" {
#endif

ESP_EVENT_DECLARE_BASE(HA_CFG_EVENT_BASE);
extern esp_event_loop_handle_t ha_cfg_event_handle;

enum HA_CFG_EVENT
{
	HA_CFG_SET,
	HA_CFG_BROKER_CHANGED,
	HA_CFG_EVENT_ALL,
};

typedef struct
{
	char broker_url[128];
	char client_id[16];
	char username[32];
	char password[64];
} ha_cfg_interface;

int indicator_ha_model_init(void);
int indicator_ha_view_init(void);
esp_err_t ha_cfg_get(ha_cfg_interface* cfg);
esp_err_t ha_cfg_set(ha_cfg_interface* cfg);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif /*INDICATOR_HA_H*/