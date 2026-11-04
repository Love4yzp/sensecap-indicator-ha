# PROJECT KNOWLEDGE BASE

Scope: SenseCAP Indicator Home Assistant firmware, with emphasis on the ESP32S3 screen-side code.

## Stack

- ESP-IDF v5.5.6 (fixed implementation baseline)
- ESP32-S3 owns display, touch, Wi-Fi, Entity Core, Native API, MQTT, Home Assistant control logic, command console, display settings, and UART ingress from RP2040.
- RP2040 owns onboard/Grove sensor acquisition.
- LVGL 9 is managed by ESP Component Manager. The runtime UI is handwritten in domain view/screen components under `main/`, with reusable image/font assets under `main/assets/`.

## Starter Project Guidance

The SenseCAP Indicator starter project is a reference for Agent-Driven workflow, not a source of driver code for this firmware.

Use these starter ideas:

- Agent-readable project knowledge files.
- Explicit module boundaries.
- Small verification scripts.
- A path toward no-hardware checks.

Do not import starter BSP/display/touch/RP2040 driver code into this project unless a later scoped test proves a specific replacement is necessary. This repository's working firmware behavior is the source of truth.

## Boot Sequence

Start at `main/main.c`:

1. `bsp_board_init()`
2. `lv_port_init()`
3. `view_event_init()`
4. `indicator_view_init()`
5. `indicator_model_init()`

View initialization creates the nav tileview and domain view/screen components. Model initialization starts storage, button, display, RP2040, sensor, command, Entity Core, Home Assistant, and Native API logic. The shared MQTT lifecycle controller starts before Wi-Fi so early GOT_IP events cannot strand pending MQTT starts.

## Where To Look

| Task | Start Here |
| --- | --- |
| Boot order | `main/main.c` |
| Runtime module wiring | `main/indicator_model.c`, `main/indicator_view.c` |
| Shared event ids and payloads | `main/view_data_types.h` |
| Cross-domain event delivery | `main/view_event/view_event.h` |
| Navigation and page roots | `main/nav/nav.h`, `main/nav/nav.c` |
| LVGL image/font assets | `main/assets/` |
| LVGL port and locking | `main/lv_port.h`, `main/lv_port.c` |
| Wi-Fi model/view | `main/wifi/` |
| MQTT controller | `main/mqtt/mqtt.c`, `main/mqtt/mqtt.h` |
| Home Assistant model/view | `main/ha/` |
| HA entity identity and authoritative state | `main/ha/entity_registry.*`, `main/ha/entity_core.*` |
| Native API, mDNS, Noise, and configuration | `main/ha/native_api*`, `main/ha/native_config.*`, `main/ha/README.md` |
| Display settings | `main/display/` |
| RP2040 UART ingress | `main/rp2040/rp2040.c`, `main/rp2040/rp2040.h` |
| Built-in sensor cache/parser | `main/sensor/` |
| Local development checks | `scripts/dev_check.py`, `scripts/architecture_scan.py` |

## Architecture Rules

- Preserve current product behavior unless a task explicitly says otherwise.
- ESP32S3 receives sensor data from RP2040. Do not add direct Grove drivers on ESP32S3.
- Current runtime domains live in vertical slices such as `main/ha/`, `main/wifi/`, `main/sensor/`, `main/display/`, `main/rp2040/`, `main/mqtt/`, `main/storage/`, `main/cmd/`, and `main/btn/`; do not reintroduce legacy compatibility layers.
- View/screen files may call LVGL APIs. Model files should remain UI-free.
- Model files should not gain new LVGL object ownership.
- Background tasks and event handlers must update LVGL through the documented LVGL lock/deferral pattern for the current code.
- Cross-domain `VIEW_EVENT_*` traffic must use `view_event.h`; queue, timeout,
  retry, coalescing, and transport details belong inside that deep module.
- Entity Registry defines the stable 4 sensor/6 switch/2 number identities; Entity Core owns their RAM state. Accepted local control commands remain applied while MQTT or Home Assistant is offline. Transport-specific identifiers and I/O stay in their adapters.
- MQTT and Native API have independent enable keys and lifecycles and may run together. With no `native-enabled` NVS key, Native defaults to enabled; an explicit stored false disables it. Fresh MQTT enablement still requires a valid stored broker configuration.
- `view_data_wifi_st.has_ip` gates LAN transports and mDNS. `is_network` is only the public-Internet ping result.
- Control values and sensor validity are runtime RAM state, not persisted NVS state. Transport restart or reconnect must not reset Entity Core.
- MQTT topics and Home Assistant payload compatibility are product behavior. Native without a PSK is plaintext and unauthenticated; configuring a PSK makes Noise mandatory. See `main/ha/README.md` before changing this security boundary.
- Keep runtime refactors incremental; this project should remain buildable after each patch.

## Verification

Preferred local checks:

```bash
python3 scripts/dev_check.py --skip-build
python3 scripts/dev_check.py
```

Architecture-only check:

```bash
python3 scripts/architecture_scan.py
```

The firmware build path is:

```bash
./dev build
```

`./dev build` deletes `build/` before building by default (pass `--no-clean` for an incremental build). Preserve this clean-by-default behavior unless the user explicitly approves changing it.
