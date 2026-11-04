#ifndef HA_H
#define HA_H

/* Existing HA/MQTT umbrella header.
 * Prefer domain-specific headers when only one slice is needed:
 *   ha_config.h  — broker NVS config types and API
 *   ha_mqtt.h    — MQTT lifecycle, event loops, HA_CFG_EVENT
 *   ha_sensor.h  — sensor entity and MQTT routing
 *   ha_switch.h  — control entity and MQTT routing
 * Entity Core and Native API use entity_core.h and native_api.h directly.
 */
#include "view_data.h"
#include "ha_config.h"
#include "ha_mqtt.h"
#include "ha_sensor.h"
#include "ha_switch.h"

#endif /* HA_H */
