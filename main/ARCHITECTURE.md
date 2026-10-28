# Firmware Architecture

SenseCAP Indicator firmware for ESP32-S3 + RP2040. ESP-IDF v5.5.6, FreeRTOS, LVGL 9 via ESP Component Manager, esp-mqtt.

---

## Directory Map

```
main/
  main.c                  Entry point. Initializes the view-event module, then calls view init and model init.
  indicator_model.c       Model init orchestrator for storage, button, display, RP2040, sensor, command, Wi-Fi, MQTT, and HA.
  indicator_view.c        View init orchestrator. Creates nav tileview, then domain views/components.
  indicator_enabler.h     Aggregates all module headers; define-guards drive conditional init.
  view_data.h             Transitional forwarding header; do not use in new code.
  view_data_types.h       Authoritative VIEW_EVENT_* ids and payload data types.
  view_event/             Deep delivery module: semantic interface, policy, stats, and ESP Event adapter.
  lv_port.c               LVGL display/touch port. Owns lv_port_sem_take/give for LVGL thread safety.

  nav/                    lv_tileview navigation for the main swipeable screens.
  assets/                 LVGL 9 image/font descriptors used by handwritten screen components.

  ha/                     Home Assistant domain: broker config, MQTT lifecycle, sensors, switches, screen widgets.
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
       → esp32_rp2040_init → indicator_sensor_init
       → indicator_cmd_init
       → indicator_wifi_model_init → indicator_mqtt_init → indicator_ha_model_init
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

| Semantic category | Queue policy | Full/error outcome | Observable counters |
| --- | --- | --- | --- |
| Critical command | Three fixed 5 ms adapter attempts; 15 ms maximum transport wait | `VIEW_EVENT_DELIVERY_REJECTED`, centralized error log; caller must not advance local state | enqueued, retries, rejected, dispatched |
| Replaceable state | Zero-wait synchronization and queue token; latest value coalesces by event/key | `VIEW_EVENT_DELIVERY_DROPPED`, centralized error log | enqueued, coalesced, dropped, dispatched, synchronization busy |

Sensor state is keyed by sensor type, and Home Assistant sensor state by entity
index. Other state events keep one latest value per event. Payloads are copied
before publication returns and are valid to subscribers only during the
handler call.

The full producer/consumer/payload/policy manifest is in
`docs/stability/milestone-1-event-reliability.md`.

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
| `lv_port.c`, `components/bsp/` | Display/touch hardware behavior |
| `nav/nav.c` | Main page container ownership |
| `main/assets/` | Shared image/font descriptors |

Verifying changes:

```bash
python3 scripts/dev_check.py --skip-build
python3 scripts/test_ha_switch_protocol.py   # for HA/MQTT protocol changes
./dev build                                  # full firmware build when needed
```

Adding a new event: add it to `view_data_types.h` before `VIEW_EVENT_ALL`, add
its category/payload/key descriptor in `view_event.c`, and update the stability
manifest. `scripts/architecture_scan.py` rejects the legacy transport symbols,
external use of the private adapter, and ESP Event transport calls outside the
production adapter.

The Wi-Fi domain owns `wifi_scan_worker`: the view-event handler only submits
a latest-value request and returns. Its queue-length-one production adapter
runs the synchronous ESP-IDF scan on a Wi-Fi-owned task, so the view-event
delivery task never scans or self-posts a result while handling a request.

Adding a new page: add a tile in `nav/nav.h`, include the owning directory in `main/CMakeLists.txt`, build widgets in that domain's view/screen file, and call the view init function from `indicator_view.c`.
