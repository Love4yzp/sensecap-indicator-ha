# Milestone 1 stage 1: view-event reliability

This stage introduces a deep module at the view-event delivery seam. The
module's interface is the only supported way to publish or subscribe to
`VIEW_EVENT_*` traffic. Callers choose semantic intent—critical command or
replaceable state—but do not choose queue types, event-loop handles, retries,
or timeouts.

This document records the pre-change inventory before defining or implementing
the module interface.

## Pre-change transport

- `main/main.c` creates a private ESP event loop with a queue size of 10, then
  exports its `view_event_handle` globally through `view_data.h`.
- Producers in 13 runtime files call `esp_event_post_to()` directly. Every
  active view-event post supplies `portMAX_DELAY`.
- Consumers register directly with
  `esp_event_handler_instance_register_with()`.
- The same event-loop task dispatches UI updates, Wi-Fi commands, MQTT/HA
  controller work, and synchronous Wi-Fi scans.
- Payloads are copied by ESP Event before the producer returns.

The resulting interface is shallow: each caller must know the event-loop
implementation, choose an unbounded wait policy, know payload lifetime rules,
and handle transport errors independently.

## Producer contexts and current risk

| Producer context | Current sources | Risk when the 10-entry queue is full |
| --- | --- | --- |
| LVGL callbacks | Wi-Fi dialogs, settings, display controls, HA switch/arc/slider controls | The LVGL task can block forever, freezing rendering and touch handling. |
| ESP timer callback | Display sleep timer | The timer task can block forever while trying to publish screen state. |
| RP2040 UART task | Sensor parser through `sensor_model.c` | Continuous sensor ingress can block behind slow UI/network consumers and stop draining UART data. |
| Wi-Fi/IP callbacks and Wi-Fi task | Link status, connection result, scan result, initial screen | ESP Wi-Fi callback processing or the Wi-Fi worker can block behind the view queue. |
| MQTT callback/task | HA sensor and switch updates, HA address display | Broker callback processing or restore work can block behind UI work. |
| Button callbacks | Screen control, shutdown, factory reset | A physical control callback can block forever; repeated press-up delivery can post shutdown more than once. |
| Model initialization | Initial display/configuration events | Boot can stall indefinitely if consumers stop draining the queue. |

`portMAX_DELAY` used for model mutexes, driver receive loops, and the separate
HA/MQTT configuration event loops is recorded but is not a view-queue policy.
This stage removes unbounded waits from view-event producers; it does not
change BSP/driver synchronization or unrelated event buses.

## Event policy inventory

Payload lifetime after migration remains callback-scoped: a consumer may read
the payload only while its subscribed handler is running.

| Event | Producers | Consumers | Payload | Stage policy |
| --- | --- | --- | --- | --- |
| `VIEW_EVENT_SCREEN_START` | Wi-Fi model, settings view | Wi-Fi, display, HA configuration views | `uint8_t` / `enum start_screen` | Critical command; bounded enqueue with explicit rejection. |
| `VIEW_EVENT_TIME` | currently unused | currently unused | `bool` | Replaceable state. |
| `VIEW_EVENT_WIFI_ST` | Wi-Fi callbacks/task | Wi-Fi view, MQTT controller, HA MQTT controller | `struct view_data_wifi_st` | Replaceable state, one latest value. |
| `VIEW_EVENT_CITY` | currently unused | currently unused | documented city value | Replaceable state. |
| `VIEW_EVENT_SENSOR_DATA` | RP2040 UART parser via sensor model | Sensor view and HA sensor publisher | `struct view_data_sensor_data` | Replaceable state, latest value per sensor type. |
| Sensor history events | currently unused | currently unused | trigger/history payload per existing manifest | Replaceable state per event. |
| `VIEW_EVENT_WIFI_LIST` | Wi-Fi scan consumer | Wi-Fi view | `struct view_data_wifi_list` | Critical result; rejection is counted and logged. |
| `VIEW_EVENT_WIFI_LIST_START` | currently unused | Wi-Fi view | `NULL` | Critical command/notification. |
| `VIEW_EVENT_WIFI_LIST_REQ` | Wi-Fi views | Wi-Fi model and Wi-Fi view | `NULL` | Critical user command. |
| `VIEW_EVENT_WIFI_CONNECT` | Wi-Fi connect view | Wi-Fi model and Wi-Fi view | `struct view_data_wifi_config` | Critical user command. |
| `VIEW_EVENT_WIFI_CONNECT_RET` | Wi-Fi callbacks | Wi-Fi view | `struct view_data_wifi_connet_ret_msg` | Critical command result. |
| `VIEW_EVENT_WIFI_CFG_DELETE` | Wi-Fi details view | Wi-Fi model and Wi-Fi view | `NULL` | Critical destructive command. |
| Time configuration events | currently unused | currently unused | `struct view_data_time_cfg` | Critical configuration command. |
| `VIEW_EVENT_DISPLAY_CFG` | Display model | Display view | `struct view_data_display` | Replaceable state. |
| `VIEW_EVENT_BRIGHTNESS_UPDATE` | Display LVGL slider | Display model | `int32_t` | Replaceable state, latest value. |
| `VIEW_EVENT_DISPLAY_CFG_APPLY` | Display LVGL callbacks | Display model | `struct view_data_display` | Critical configuration command. |
| `VIEW_EVENT_SHUTDOWN` | Button release | Wi-Fi model and RP2040 controller | `NULL` | Critical command; bounded enqueue and explicit result. |
| `VIEW_EVENT_FACTORY_RESET` | Button hold | currently no registered consumer; reset timer performs erase/restart | `NULL` | Critical command; preserve current publication for compatibility. |
| `VIEW_EVENT_SCREEN_CTRL` | Button and display timer | currently no registered consumer | `bool` | Replaceable state. |
| `VIEW_EVENT_MQTT_ADDR_CHANGED` | HA configuration LVGL callback | HA configuration view | `NULL` | Critical configuration command. |
| `VIEW_EVENT_HA_ADDR_DISPLAY` | HA MQTT initialization | HA configuration view | `NULL` | Replaceable state/refresh notification. |
| `VIEW_EVENT_HA_SENSOR` | MQTT callback | no current consumer | `struct view_data_ha_sensor_data` | Replaceable state, latest per HA sensor index. |
| `VIEW_EVENT_HA_SWITCH_ST` | HA LVGL widgets | HA switch controller | `struct view_data_ha_switch_data` | Critical user command; no debounce or state-echo change in this stage. |
| `VIEW_EVENT_HA_SWITCH_SET` | MQTT callback and restore task | HA switch controller/view | `struct view_data_ha_switch_data` | Critical inbound command; preserve ordering and every value. |

## Interface and seam decision

The external seam will expose four operations:

1. initialize the module;
2. subscribe a handler to an event;
3. post a critical command;
4. publish replaceable state and read delivery statistics.

The interface validates event category and payload size. A caller cannot
provide a timeout or access the ESP event-loop handle.

The implementation owns:

- the ESP Event production adapter and its fixed bounded command wait;
- state coalescing keyed by event semantics;
- payload copying and callback-scoped lifetime;
- subscriber dispatch;
- counters for rejected commands, merged states, dropped states, invalid
  publications, and successful dispatches;
- centralized failure logging.

A controllable host-test adapter occupies the same internal seam as the ESP
Event adapter. Tests drive queue-full and delayed-drain behavior through the
module interface; the adapter seam remains private to the implementation.

## Sleep/shutdown idempotency

The current button code uses independent global flags and posts shutdown from
the press-up callback whenever the accumulated hold time is in range. A
duplicate press-up callback, or a release after the factory-reset threshold,
can therefore repeat or overlap shutdown work.

The button hold transition will become an in-process state module. Its
interface reports actions (`prepare sleep`, `restart`, `factory reset`, or
`shutdown`) while hiding thresholds and per-press idempotency. The shutdown
action is emitted at most once per press and never after factory reset. The
existing display-off preview, short-click restart, Wi-Fi stop, RP2040 shutdown,
and restart behavior remain outside that module and unchanged.

## Explicitly deferred

- credential log redaction;
- HA widget state-echo suppression;
- HA slider/arc debounce or persistence policy;
- RP2040 firmware or wire-protocol changes;
- BSP, LCD/touch, DMA/timing, partition, or eFuse changes.
