# HA Domain — Vertical Slice Architecture

Home Assistant integration. This domain is the pilot for the project's target architecture:
each feature slice owns its full stack (data model + MQTT protocol + UI component).

---

## Files and Responsibilities

| File | Lines | Owns |
|------|-------|------|
| `ha.h` | 50 | Public API for the entire domain. Include this, not individual slice headers. |
| `ha_mqtt.c` | ~200 | MQTT client lifecycle: connect/disconnect/reconnect. Routes `MQTT_EVENT_DATA` to sensor and switch slices. Owns `mqtt_ha_instance`. |
| `ha_config.c` | ~154 | Broker NVS config (`ha_cfg_get`/`ha_cfg_set`) + IP address modal view. |
| `ha_sensor.c` | ~128 | Sensor entity metadata, subscribes to sensor MQTT topics, publishes built-in sensor readings, routes incoming sensor JSON to `VIEW_EVENT_HA_SENSOR`. |
| `ha_switch.c` | Switch entity metadata, in-memory authoritative state, MQTT publish/subscribe, and generation-tagged publish results. UI-free. |
| `ha_switch_sync.c` | Pure generation/debounce/result state module. Owns stale-result filtering and rollback decisions. |
| `ha_switch_screen.c` | Builds and owns all 8 switch widgets, LVGL timers, and direct subscriptions to model state/publish results. |
| `ha_switch_screen.h` | Single idempotent `create` interface. |

---

## Init Sequence

```
indicator_ha_view_init()    (called from indicator_view.c)
  ha_config_view_init()     register VIEW_EVENT_MQTT_ADDR_CHANGED + VIEW_EVENT_HA_ADDR_DISPLAY
  ha_switch_screen_create() builds switch widgets on NAV_TILE_HA_CTRL and NAV_TILE_HA_MIX
                            subscribe to HA switch model state + publish results

indicator_ha_model_init()   (called later from indicator_model.c)
  ha_sensor_init()          register VIEW_EVENT_SENSOR_DATA handler
  ha_switch_init()          init entities, publish zero startup values,
                            register VIEW_EVENT_HA_SWITCH_ST handler
  create ha_cfg_event_handle
  init mqtt_ha_instance
  register VIEW_EVENT_WIFI_ST handler
```

---

## Event Flows

```
User changes a switch widget
  → ha_switch_screen.c: LV_EVENT_VALUE_CHANGED
  → toggle publishes immediately; arc/slider debounce for 200 ms
  → post VIEW_EVENT_HA_SWITCH_ST {index, value, generation}
  → ha_switch.c: publish to MQTT topic_state
  → update in-memory controller state on client acceptance
  → post VIEW_EVENT_HA_SWITCH_PUBLISH_RESULT
  → screen confirms or rolls back the matching generation

MQTT broker sends switch command
  → ha_mqtt.c: MQTT_EVENT_DATA
  → ha_switch_on_mqtt_data()
  → update latest in-memory model value
  → publish keyed VIEW_EVENT_HA_SWITCH_SET {index, value}
  → ha_switch_screen.c cancels local timers and applies under LVGL lock

WiFi connects
  → VIEW_EVENT_WIFI_ST {is_network=true}
  → ha_mqtt.c: post MQTT_APP_START to mqtt_app_event_handle
  → mqtt/mqtt.c: calls mqtt_ha_instance.mqtt_starter()
  → ha_mqtt.c: _mqtt_ha_start() reads NVS config, creates MQTT client

MQTT connects
  → MQTT_EVENT_CONNECTED
  → ha_sensor_subscribe() — subscribe to sensor topics
  → ha_switch_subscribe() — subscribe to switch topics, start restore task

User changes broker IP
  → ha_config.c: posts VIEW_EVENT_MQTT_ADDR_CHANGED
  → ha_config.c: validates, saves NVS, posts HA_CFG_BROKER_CHANGED
  → ha_mqtt.c: stop/set_uri/start MQTT client
```

---

## Known Issues

- `VIEW_EVENT_HA_SENSOR` is posted by `ha_sensor.c` but **has no consumer**. The HA sensor data display screen is not yet wired up. See the manifest comment in `view_data.h`.

---

## Adding a New Switch Widget Type

1. Add a new `_update_xxx()` function in `ha_switch_screen.c`
2. Add a slot entry in `ha_switch_screen_create()` pointing to the locally-created widget and updater
3. Extend `CONFIG_HA_SWITCH_ENTITY_NUM` in `home_assistant_config.h` if adding a new entity
4. Choose immediate or debounced input semantics explicitly

## Extending to a New Feature Slice

Follow this pattern:
1. `ha_yyy.c` — data + MQTT logic, registers event handlers in `ha_yyy_init()`
2. `ha_yyy_screen.c` — LVGL component, `create`/`update`/`destroy` interface
3. Declare cross-module functions in `ha.h`
4. Call `ha_yyy_init()` from `indicator_ha_model_init()` in `ha_mqtt.c`
5. Call `ha_yyy_screen_create()` from `indicator_ha_view_init()` in `ha_mqtt.c`
