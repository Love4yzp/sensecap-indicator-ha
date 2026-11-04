# Firmware Architecture

SenseCAP Indicator firmware for ESP32-S3 + RP2040. ESP-IDF v5.5.6, FreeRTOS, LVGL 9 via ESP Component Manager, esp-mqtt, nanopb, mDNS, and Noise.

---

## Directory Map

```
main/
  main.c                  Entry point. Initializes the view-event module, then calls view init and model init.
  indicator_model.c       Model init orchestrator for storage, button, display, RP2040, sensor, command, Entity Core, HA/Native, Wi-Fi, and MQTT.
  indicator_view.c        View init orchestrator. Creates nav tileview, then domain views/components.
  indicator_enabler.h     Aggregates all module headers; define-guards drive conditional init.
  view_data.h             Transitional forwarding header; do not use in new code.
  view_data_types.h       Authoritative VIEW_EVENT_* ids and payload data types.
  view_event/             Deep delivery module: semantic interface, policy, stats, and ESP Event adapter.
  lv_port.c               LVGL display/touch port. Owns lv_port_sem_take/give for LVGL thread safety.

  nav/                    lv_tileview navigation for the main swipeable screens.
  assets/                 LVGL 9 image/font descriptors used by handwritten screen components.

  ha/                     Home Assistant domain: Entity Registry/Core, MQTT and Native adapters, Noise/mDNS, configuration, and screen widgets.
  wifi/                   Wi-Fi domain: scanning, connection state, list/connect modals, status icon.
  sensor/                 Built-in sensor cache/parser and sensor data view.
  display/                LCD backlight, sleep mode, and display settings view.
  rp2040/                 UART/COBS ingress from the RP2040 co-processor.
  btn/                    Physical button handling and per-press idempotency state.
  mqtt/                   Shared MQTT client lifecycle controller.
  storage/                NVS helpers.
  cmd/                    Serial command interface.

  util/
    cobs.*                COBS encode/decode for RP2040 UART framing.
    indicator_util.*      IP address helpers.
```

New work should go into the owning vertical domain, not into legacy compatibility folders.

---

## Boot Sequence

```
app_main()
  1. bsp_board_init()
  2. lv_port_init()
  3. view_event_init()
  4. indicator_view_init()
       nav_init()
       → indicator_display_view_init
       → view_sensor_init
       → indicator_wifi_view_init
       → indicator_ha_view_init
  5. indicator_model_init()
       indicator_nvs_init → indicator_btn_init → indicator_display_init
       → esp32_rp2040_init → indicator_sensor_init → indicator_cmd_init
       → entity_core_init → indicator_ha_model_init → native_api_init
       → indicator_mqtt_init → indicator_wifi_model_init
```

View initialization runs before model initialization in the current code. Screen components must tolerate initial empty state and update when model events arrive.

---

## View-event delivery module

Cross-domain `VIEW_EVENT_*` traffic goes through the public
`view_event/view_event.h` interface:

```c
bool view_event_init(void);
bool view_event_subscribe(enum view_event_id, view_event_handler_t, void *);
view_event_delivery_t view_event_post_command(
    enum view_event_id, const void *payload, size_t payload_size);
view_event_delivery_t view_event_publish_state(
    enum view_event_id, const void *payload, size_t payload_size);
void view_event_stats_get(struct view_event_stats *);
```

This is a deep module: callers state whether they are sending a critical
command or replaceable state, while the implementation owns queue selection,
timeouts, payload validation/copying, coalescing keys, dispatch, failure
logging, and counters. Callers never receive an ESP Event handle and cannot
choose a FreeRTOS wait duration.

The internal `view_event_adapter.h` seam separates policy from transport.
Firmware uses `view_event_esp_adapter.c`; host tests substitute a controllable
adapter while exercising only the public module interface. Runtime domains
must not include the adapter header or call the view transport directly.
Subscriptions are append-only after startup registration. Dispatch reads the
published subscription count without taking the mutable-state lock, so a
transport-accepted command cannot be discarded by later lock contention.

| Semantic category | Queue policy | Full/error outcome | Observable counters |
| --- | --- | --- | --- |
| Critical command | Three fixed 5 ms adapter attempts; 15 ms maximum transport wait; accepted commands dispatch through the append-only subscriber snapshot | `VIEW_EVENT_DELIVERY_REJECTED`, centralized error log; caller must not advance local state | enqueued, retries, rejected, dispatched after handler execution |
| Replaceable state | Zero-wait producer synchronization and queue token; latest value coalesces by event/key; a consumed token moves to the adapter retry queue while internal synchronization is busy | `VIEW_EVENT_DELIVERY_DROPPED`, centralized error log | enqueued, coalesced, dropped, dispatched after handler execution, synchronization busy |

Built-in sensor state is keyed by sensor type; external HA sensor state and
committed HA control state are keyed by their respective indices. Other state
events keep one latest value per event. Payloads are copied before publication
returns and are valid to subscribers only during the handler call.

The full producer/consumer/payload/policy manifest is in
`docs/stability/milestone-1-event-reliability.md`.

## Home Assistant Entity Core and transports

`ha/entity_registry.c` defines stable identities and shared metadata for 4 local
sensors, 6 switches, and 2 numbers. The two numbers occupy legacy UI/MQTT slots
5 and 8. `ha/entity_core.c` exclusively owns authoritative values, validity,
source, and monotonically increasing `state_version`. It validates and serially
commits commands, copies snapshots out under its lock, and invokes observers
after unlocking. Registry/Core contain no LVGL, MQTT, or socket I/O.

Control values are RAM-only and cold-start at zero. Sensors begin with
`valid=false` and Native exposes `missing_state` until an RP2040 sample arrives.
There is no runtime-state NVS persistence. Restarting or reconnecting either
transport does not reset Core.

### Existing LVGL seam and local commands

- `VIEW_EVENT_HA_SWITCH_ST` carries a generation-tagged local UI command to
  `ha_switch.c`, which submits it to Core.
- `VIEW_EVENT_HA_SWITCH_SET` is replaceable committed Core state keyed by control
  slot, regardless of whether the source was boot, local UI, MQTT, or Native.
- `VIEW_EVENT_HA_SWITCH_PUBLISH_RESULT` retains its legacy name but reports Core
  validation/commit acceptance and the authoritative value, not MQTT publish
  acceptance.

`ha_switch_sync` remains the pure generation/debounce/result module.
`ha_switch_screen.c` retains `switch_slot_t`, `widget_update_fn`, `suppress_post`,
and one-shot LVGL timers. Toggle input submits immediately; arc and slider input
uses a 200 ms quiet-period debounce. Core model state cancels pending local
timers and is applied with command emission suppressed.

A local command that Core accepts remains applied while MQTT or Home Assistant
is offline; transport failure does not roll it back. View-event rejection,
invalid Core input, or the existing 2 second local-result timeout can still
restore the last accepted UI value. `ha_switch.c` remains UI-free and does not
own LVGL objects.

### MQTT and Native lifecycle

MQTT and Native are independent adapters with separate NVS enable keys and
idempotent lifecycle paths. They can run separately or together; enabling,
disabling, or reconfiguring one does not restart the other or clear Core. Both
read current Core snapshots when a subscriber/reconnect needs initial state.

A missing `native-enabled` key defaults Native to enabled for zero-broker HA
setup, while an explicit stored false disables it. A missing `mqtt-enabled` key
defaults MQTT to enabled only if a valid stored broker configuration exists.
Existing MQTT topics, JSON keys, payload formats, and QoS remain unchanged.

`view_data_wifi_st.has_ip` is the LAN-ready signal set by
`IP_EVENT_STA_GOT_IP`; it gates MQTT, Native, and mDNS. `is_network` is the
separate public-Internet ping result and is not a local transport prerequisite.
Thus loss of WAN need not stop LAN control.

Native listens on TCP 6053, advertises `_esphomelib._tcp.local.` via mDNS, and
uses a stable base-MAC-derived name/MAC identity. Manual IP/hostname setup still
works when mDNS is unavailable. With no PSK (the default), Native is plaintext
and unauthenticated; this is a LAN security risk. Configuring a fixed 32-byte
PSK makes Noise NNpsk0（Curve25519、ChaChaPoly、SHA256） mandatory and rejects plaintext
fallback. PSK provisioning/rotation and NVS/flash encryption are not provided.

See `main/ha/README.md` for module/configuration details and
`docs/stability/milestone-2-ha-state-sync.md` for synchronization scenarios.

## Other event loops

| Handle | Base | Created in | Purpose |
|--------|------|------------|---------|
| `mqtt_app_event_handle` | `MQTT_APP_EVENT_BASE` | `mqtt/mqtt.c` | MQTT client lifecycle commands |
| `ha_cfg_event_handle` | `HA_CFG_EVENT_BASE` | `ha/ha_mqtt.c` | HA broker config changes |
| `cmd_cfg_event_handle` | `CMD_CFG_EVENT_BASE` | `cmd/cmd.c` | Serial command events |
| default event loop | `WIFI_EVENT`, `IP_EVENT` | `wifi/wifi_model.c` | ESP-IDF Wi-Fi driver events |

The view-event ids and payload types live in `view_data_types.h`. When changing
an event payload or delivery category, update its module validation, every
producer and consumer, and the stability manifest together.

---

## LVGL Thread Safety

LVGL is not thread-safe. Any code touching widget state outside the LVGL task must hold the semaphore:

```c
lv_port_sem_take();
// ... lv_obj_* calls ...
lv_port_sem_give();
```

Allowed LVGL ownership is intentionally narrow:

| Area | LVGL access |
|------|-------------|
| `lv_port.[ch]` | Display/touch port and LVGL lock |
| `nav/nav.[ch]` | Tileview/page-root containers |
| `*_view.c` and `*_screen.c` files | Domain widgets and callbacks |
| model/controller files | No LVGL object ownership |

### LVGL health diagnostics

`lv_port.c` owns the LVGL-side heartbeat adapter. A periodic LVGL timer invokes
`lvgl_health_heartbeat()` from the real LVGL task, allowing
`main/lvgl_health/` to capture `xTaskGetCurrentTaskHandle()` without a task-name
lookup. The health module may observe the captured task's state and stack
low-water mark, but it must not call LVGL APIs, mutate the LVGL task, or restart
the device. Stall policy is isolated in the host-testable
`lvgl_health_state.[ch]` module and documented in
`docs/stability/milestone-3-lvgl-health.md`.

---

## Architectural Pattern

The branch uses vertical domain slices. Each domain owns its model, view, and supporting screen components behind a small public header:

- `ha/ha.h`
- `wifi/wifi.h`
- `sensor/sensor.h`
- `display/display.h`
- `rp2040/rp2040.h`
- `mqtt/mqtt.h`
- `storage/storage_nvs.h`
- `cmd/cmd.h`
- `btn/btn.h`

Domain-to-domain UI/data communication should go through the `view_event`
interface or a documented domain API. Do not reintroduce a horizontal
compatibility layer, an exported transport handle, or generated-UI globals.

Screen components follow the local ownership pattern:

- `create()` or `*_init()` builds widgets under a passed parent or a `nav_get_tile()` container.
- `update()` applies state; callers must hold the LVGL lock unless the component documents that it locks internally.
- `destroy()` is only needed for components with dynamic modal/widget lifetime.

---

## Agent Working Guidelines

Before modifying an event, read the manifest in
`docs/stability/milestone-1-event-reliability.md` to find all producers,
consumers, payloads, and delivery policies.

Before modifying a module, check its blast radius:

| File/area | Blast radius |
|-----------|-------------|
| `view_data_types.h`, `view_event/` | Cross-domain event contract and delivery policy |
| `indicator_model.c`, `indicator_view.c` | Boot/init ordering |
| `ha/entity_registry.*`, `ha/entity_core.*` | Stable HA identities and all local authoritative state |
| `ha/native_api*`, `ha/native_config.*` | HA wire compatibility, LAN exposure, and PSK security boundary |
| `lv_port.c`, `components/bsp/` | Display/touch hardware behavior |
| `nav/nav.c` | Main page container ownership |
| `main/assets/` | Shared image/font descriptors |

Verifying changes:

```bash
python3 scripts/dev_check.py --skip-build
python3 scripts/test_entity_core.py
python3 scripts/test_ha_transport_config.py
python3 scripts/test_native_api_protocol.py
python3 scripts/test_native_api_noise.py
python3 scripts/test_ha_switch_sync.py       # pure HA generation/debounce state
python3 scripts/test_ha_switch_protocol.py   # Core/UI plus unchanged MQTT wire contract
./dev build                                  # full firmware build when needed
```

The optional pinned `aioesphomeapi` integration script is a separate client-level
check, not evidence of real Home Assistant or full hardware acceptance. Keep its
results distinct from the pending matrix in
`docs/stability/hardware-smoke-checklist.md`.

Adding a new event: add it to `view_data_types.h` before `VIEW_EVENT_ALL`, add
its category/payload/key descriptor in `view_event.c`, and update the stability
manifest. `scripts/architecture_scan.py` rejects the legacy transport symbols,
external use of the private adapter, and ESP Event transport calls outside the
production adapter. It also rejects domain code that includes the private
`wifi_scan_worker_adapter.h` seam; runtime callers use
`wifi_scan_worker.h`.

The Wi-Fi domain owns `wifi_scan_worker`: the view-event handler only submits
a latest-value request and returns. Its queue-length-one production adapter
runs the synchronous ESP-IDF scan on a Wi-Fi-owned task, so the view-event
delivery task never scans or self-posts a result while handling a request.
The request handler takes its model snapshot with zero wait. Scan, snapshot,
submission, or result-delivery failure produces
`VIEW_EVENT_WIFI_LIST_FAILED`; the view clears pending/spinner state on that
completion and retains a 15-second timeout as the final queue-pressure
fallback. Requests and completions carry a view-owned scan generation.
`wifi_scan_ui_state` classifies current, closed-modal, and stale completions,
so an old result/failure/timeout cannot mutate a newer scan's pending state or
timer.

Adding a new page: add a tile in `nav/nav.h`, include the owning directory in `main/CMakeLists.txt`, build widgets in that domain's view/screen file, and call the view init function from `indicator_view.c`.
