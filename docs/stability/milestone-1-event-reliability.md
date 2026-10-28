# Milestone 1 stage 1: view-event reliability

This stage introduces a deep module at the view-event delivery seam. The
module's interface is the only supported way to publish or subscribe to
`VIEW_EVENT_*` traffic. Callers choose semantic intent—critical command or
replaceable state—but do not choose queue types, event-loop handles, retries,
or timeouts.

This document records the pre-change inventory and the implemented stage-1
contract.

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

## Implemented module

Public interface (`main/view_event/view_event.h`):

```c
bool view_event_init(void);
bool view_event_subscribe(enum view_event_id, view_event_handler_t, void *);
view_event_delivery_t view_event_post_command(
    enum view_event_id, const void *payload, size_t payload_size);
view_event_delivery_t view_event_publish_state(
    enum view_event_id, const void *payload, size_t payload_size);
void view_event_stats_get(struct view_event_stats *);
```

The interface is semantic rather than transport-shaped. Producers cannot see
the ESP Event loop, FreeRTOS queue, adapter lock, or timeout. The module
validates category and exact payload size before passing data through its
internal adapter seam.

The ESP production adapter keeps the existing queue depth (10) and task stack
(10240 bytes). Critical commands use three module-owned 5 ms attempts, for a
maximum 15 ms transport wait. Replaceable state uses zero-wait synchronization,
a zero-wait token, and module-owned state slots. Slot mutation and token
acceptance are serialized so concurrent producers cannot report a coalesced
value that has no queued delivery token. If the delivery task consumes a state
token while internal synchronization is busy, the production adapter moves
that token to a dedicated retry queue sized to the complete slot set. Retry
attempts remain bounded individually and never block a producer.

Subscription registration is append-only during startup. A release/acquire
publication of the subscriber count lets dispatch copy the stable prefix
without the mutable-state lock. Consequently, a command counted as enqueued
cannot later disappear because subscription synchronization was busy.

State keys are:

- `VIEW_EVENT_SENSOR_DATA`: `sensor_type`;
- `VIEW_EVENT_HA_SENSOR`: `index`;
- all other state events: one latest value per event.

`view_event_stats_get()` reports command enqueued/retries/rejected/dispatched,
state enqueued/coalesced/dropped/dispatched, internal synchronization-busy, and
invalid publication counts. Rejected commands and dropped states also produce
a centralized error log. Category, exact payload size, and coalescing-key
metadata come from one descriptor table indexed by `view_event_id`.

`main/view_event/view_event_adapter.h` is an internal seam. The production
adapter is `view_event_esp_adapter.c`; the host regression harness supplies a
controllable adapter but invokes behavior through `view_event.h`.

## Migrated runtime call sites

The following domains now publish and subscribe only through the module
interface:

- boot initialization (`main/main.c`);
- Wi-Fi model, view, list, and connect screens;
- display model/view and settings navigation;
- RP2040 shutdown consumer and sensor model/view;
- MQTT network-state controller;
- Home Assistant configuration, MQTT, sensor, switch controller, and switch
  screen;
- physical button screen-control, shutdown, and factory-reset paths.

The MQTT controller's old `WIFI_EVENT_STA_DISCONNECTED` registration on the
private view loop was not moved to the default Wi-Fi loop. That registration
could never receive a Wi-Fi event on the old private loop; activating it now
would be a behavior change. Network-false state continues to arrive through
`VIEW_EVENT_WIFI_ST`.

## Architecture enforcement

`scripts/architecture_scan.py` now rejects:

- the removed `view_event_handle` and `VIEW_EVENT_BASE` transport symbols;
- includes of the private adapter outside `main/view_event/`;
- use of the private transport base outside the production adapter;
- direct ESP Event transport calls in the view-event core.

The event enum is also checked for an inline payload and command/state policy
comment. Unit tests cover each new boundary rule.

## Wi-Fi scan worker

`wifi_scan_worker` is a Wi-Fi-owned deep module. Its public interface only
initializes the worker and submits a connected-state snapshot. The production
adapter owns a queue of length one and overwrites pending work, so request
submission is nonblocking and repeated requests coalesce. The worker task runs
the synchronous ESP-IDF scan, de-duplicates SSIDs, builds the existing payload,
and publishes the result. The view-event delivery task only submits work and
returns; it no longer scans and posts back into its own queue.

Scan results receive three worker-owned bounded delivery attempts. The Wi-Fi
view also owns a 15-second LVGL timer for every accepted scan request; receiving
the list cancels it, while expiry clears pending state and hides the spinner.
Thus permanent view-event queue pressure cannot leave the modal indefinitely
pending, and synchronous scanning remains outside the view-event task.

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

Implemented in `main/btn/button_press_state.[ch]`: each long-press start resets
the per-press state; prepare-sleep, sleeping restart, and factory reset remain
idempotent. Shutdown uses two-phase acceptance: release keeps returning a
retryable shutdown action until `view_event_post_command()` accepts it, then
`button_press_shutdown_accepted()` consumes the action and `sleep_flag` is set.
Rejection restores the display and leaves the device awake. Release after
factory reset cannot additionally emit shutdown. Existing consumers still own
Wi-Fi stop and RP2040 shutdown behavior.

`VIEW_EVENT_FACTORY_RESET` remains a compatibility notification with no
subscriber. Its rejection is logged but cannot gate the local display-on,
three-second NVS erase timer, or restart. The per-press state still emits the
factory-reset action only once.

## Host verification

`scripts/test_view_event_delivery.py` verifies through the public interface
that:

- a simulated LVGL callback returns in under 100 ms when the controllable
  adapter reports a full command queue;
- a command retries centrally, succeeds after temporary fullness, and rejects
  after the fixed third attempt;
- an accepted command dispatches through subscription-lock contention and is
  counted as dispatched only after handler execution;
- state publication returns promptly and counts a drop when internal
  synchronization is busy;
- a consumed state token is deferred during repeated dispatch contention, and
  a later same-key value still reaches its subscriber;
- same-key sensor state coalesces while different keys retain separate
  deliveries;
- adapter-full state is dropped and counted;
- invalid category/payload use is rejected;
- the largest declared history payload fits the module contract.

`scripts/test_button_press_state.py` verifies rejected shutdown remains
retryable until explicit acceptance, new-press reset,
factory-reset/shutdown exclusion, and one restart action for a sleeping long
hold. It also statically checks that compatibility-notification rejection
cannot gate local factory reset. `scripts/test_wifi_scan_worker.py` verifies
requests return without running a scan inline, pending requests coalesce,
adapter-busy returns promptly, result construction preserves the latest
connection snapshot, result retries remain bounded under queue pressure, and
the view timeout always clears pending/spinner state.

## Explicitly deferred

- credential log redaction;
- HA widget state-echo suppression;
- HA slider/arc debounce or persistence policy;
- RP2040 firmware or wire-protocol changes;
- BSP, LCD/touch, DMA/timing, partition, or eFuse changes.
