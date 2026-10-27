# Firmware Architecture

SenseCAP Indicator firmware for ESP32-S3 + RP2040. ESP-IDF v5.5.6, FreeRTOS, LVGL 9 via ESP Component Manager, esp-mqtt.

---

## Directory Map

```
main/
  main.c                  Entry point. Creates view_event_handle, then calls view init and model init.
  indicator_model.c       Model init orchestrator for storage, button, display, RP2040, sensor, command, Wi-Fi, MQTT, and HA.
  indicator_view.c        View init orchestrator. Creates nav tileview, then domain views/components.
  indicator_enabler.h     Aggregates all module headers; define-guards drive conditional init.
  view_data.h             Shared event bus contract and VIEW_EVENT_* definitions.
  view_data_types.h       Pure data types used by the event contract.
  lv_port.c               LVGL display/touch port. Owns lv_port_sem_take/give for LVGL thread safety.

  nav/                    lv_tileview navigation for the main swipeable screens.
  assets/                 LVGL 9 image/font descriptors used by handwritten screen components.

  ha/                     Home Assistant domain: broker config, MQTT lifecycle, sensors, switches, screen widgets.
  wifi/                   Wi-Fi domain: scanning, connection state, list/connect modals, status icon.
  sensor/                 Built-in sensor cache/parser and sensor data view.
  display/                LCD backlight, sleep mode, and display settings view.
  rp2040/                 UART/COBS ingress from the RP2040 co-processor.
  btn/                    Physical button handling.
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
  3. esp_event_loop_create(&view_event_handle)
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

## Event Loops

| Handle | Base | Created in | Purpose |
|--------|------|------------|---------|
| `view_event_handle` | `VIEW_EVENT_BASE` | `main.c` | Main UI/data bus for cross-domain data flow |
| `mqtt_app_event_handle` | `MQTT_APP_EVENT_BASE` | `mqtt/mqtt.c` | MQTT client lifecycle commands |
| `ha_cfg_event_handle` | `HA_CFG_EVENT_BASE` | `ha/ha_mqtt.c` | HA broker config changes |
| `cmd_cfg_event_handle` | `CMD_CFG_EVENT_BASE` | `cmd/cmd.c` | Serial command events |
| default event loop | `WIFI_EVENT`, `IP_EVENT` | `wifi/wifi_model.c` | ESP-IDF Wi-Fi driver events |

The event manifest lives in `view_data.h` / `view_data_types.h`. When changing an event payload, update all listed producers and consumers together.

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

Domain-to-domain communication should go through `view_event_handle` or a documented domain API. Do not reintroduce a horizontal compatibility layer or generated-UI globals.

Screen components follow the local ownership pattern:

- `create()` or `*_init()` builds widgets under a passed parent or a `nav_get_tile()` container.
- `update()` applies state; callers must hold the LVGL lock unless the component documents that it locks internally.
- `destroy()` is only needed for components with dynamic modal/widget lifetime.

---

## Agent Working Guidelines

Before modifying an event, read the manifest comment in `view_data.h` to find all producers and consumers.

Before modifying a module, check its blast radius:

| File/area | Blast radius |
|-----------|-------------|
| `view_data.h`, `view_data_types.h` | Cross-domain event contract |
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

Adding a new event: add it to `view_data.h` with a full manifest comment before `VIEW_EVENT_ALL`.

Adding a new page: add a tile in `nav/nav.h`, include the owning directory in `main/CMakeLists.txt`, build widgets in that domain's view/screen file, and call the view init function from `indicator_view.c`.
