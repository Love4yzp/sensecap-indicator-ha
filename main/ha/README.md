# HA Domain — Entity Core, MQTT, and Native API

The HA domain exposes the same local entity state through the existing LVGL UI,
MQTT, and ESPHome Native API. Entity Registry/Core owns identity and state;
transport and UI code are adapters. This firmware does not run the ESPHome C++
runtime and does not implement MQTT Discovery.

## Entity and state contract

`entity_registry.c` defines stable indices, object IDs, Native keys, names,
units, and ranges for exactly 12 local entities:

- 4 sensors: temperature, humidity, tVOC, and CO₂;
- 6 switches in UI/MQTT control slots 1–4 and 6–7;
- 2 numbers in UI/MQTT slots 5 and 8, retaining the legacy `switch5` and
  `switch8` MQTT JSON keys.

`entity_core.c` is the sole authority for their current values. It validates
commands, increments `state_version`, snapshots state, and notifies observers
outside its lock. UI `generation` correlates one local operation; it is not the
Core state version.

Control values live only in RAM and cold-start at zero. Sensors start invalid
and Native reports `missing_state` until the first valid RP2040 sample. No
control or sensor state is restored from or continuously written to NVS.
Transport restart, reconnect, enable, or disable does not clear Core state.

A local UI command takes effect when Core validates and commits it, even with
no MQTT broker or Home Assistant connection. Broker/HA publish or connection
failure does not roll it back. A rejected view-event command, invalid Core
command, or missing local Core result can still make the existing UI
synchronization seam restore its last accepted value.

## Files and responsibilities

| File | Responsibility |
| --- | --- |
| `entity_registry.[ch]` | Stable shared entity descriptors and identity validation; no topic, socket, icon, or layout ownership. |
| `entity_core.[ch]` | Authoritative RAM values, validation, versions, snapshots, and two observer slots; UI-free and transport-free. |
| `ha_sensor.c` | Copies RP2040 sensor events into Core and publishes the unchanged single-key MQTT sensor JSON. External HA sensor MQTT input remains separate. |
| `ha_switch.c` | UI command/Core adapter and MQTT switch/number adapter. Publishes unchanged MQTT state when connected and emits Core command results. |
| `ha_mqtt.c` | HA MQTT client setup, configuration lifecycle, subscriptions, and model/view orchestration. |
| `ha_config.c`, `ha_tls.c` | Legacy-compatible MQTT NVS blob, independent MQTT enable key, broker modal, and MQTT TLS trust settings. |
| `ha_switch_sync.[ch]` | Existing generation, 200 ms debounce, stale-result, and local-result-timeout state machine. |
| `ha_switch_screen.c` | Existing `switch_slot_t`, `widget_update_fn`, `suppress_post`, widgets, and LVGL timers. |
| `native_config.[ch]` | Independent `native-enabled` and `native-psk` NVS settings plus strict base64 decoding; no server or secret logging. |
| `native_api.[ch]` | Native public lifecycle/status API and Wi-Fi event subscription. |
| `native_api_server.c` | Port 6053 server, two-client sessions, bounded TX queues, dirty-state delivery, keepalive, mDNS, and Core observer. |
| `native_api_protocol.[ch]` | Plaintext stream framing only; split/coalesced frames, varints, and size limits. |
| `native_api_noise.[ch]` | Noise NNpsk0 (Curve25519, ChaChaPoly, SHA-256) handshake and encrypted records using the pinned Noise libraries. |
| `native_api/` | Pinned protobuf sources, generated nanopb messages, and descriptor-derived message IDs. |

## Initialization

View initialization still runs before model initialization:

```text
indicator_ha_view_init()
  ha_config_view_init()
  ha_switch_screen_create()
    build the existing 8 control slots
    subscribe to VIEW_EVENT_HA_SWITCH_SET and VIEW_EVENT_HA_SWITCH_PUBLISH_RESULT
```

`indicator_model.c` then initializes the HA path and shared MQTT controller
before Wi-Fi, so early GOT_IP events always have a running lifecycle consumer:

```text
entity_core_init()
indicator_ha_model_init()
  ha_sensor_init()
  ha_switch_init()                 Core observer → UI + MQTT state
  create HA configuration loop and MQTT instance
  subscribe to VIEW_EVENT_WIFI_ST
native_api_init()
  create Native server task
  Core observer → Native dirty state
  subscribe to VIEW_EVENT_WIFI_ST
indicator_mqtt_init()
indicator_wifi_model_init()
```

Registry/Core therefore exists without a screen, broker, IP address, or HA
client. The screen tolerates its initial defaults and later Core snapshots.

## UI and event flow

```text
touch input
  → ha_switch_sync (toggle immediate; arc/slider 200 ms debounce)
  → VIEW_EVENT_HA_SWITCH_ST {control slot, value, generation}
  → ha_switch.c → entity_core_submit_command()
  → Core observers:
       ha_switch.c → VIEW_EVENT_HA_SWITCH_SET + MQTT state if connected
       native_api_server.c → dirty state for subscribed clients
  → VIEW_EVENT_HA_SWITCH_PUBLISH_RESULT {generation, Core accepted/value}
```

The publish-result event name is retained for compatibility, but its result now
means Core command acceptance, not ESP-MQTT acceptance. `VIEW_EVENT_HA_SWITCH_SET`
carries committed Core state from boot, local UI, MQTT, or Native. Model updates
cancel pending UI timers and are applied under the LVGL lock with `suppress_post`
so programmatic changes do not echo as new commands.

MQTT and Native commands call the same Core validation/commit path. A new MQTT
subscription publishes current Core control snapshots; a new Native state
subscription sends all 12 current snapshots. Sensor updates use the same Core
snapshot for Native and the existing MQTT payload.

## Independent transport and network lifecycle

MQTT and Native have separate enable NVS keys and independent start/stop paths;
they can be off, on, or running together. Changing one transport does not restart
the other or reset Core.

- With no `native-enabled` key, Native defaults to enabled, so a fresh device can
  be discovered by HA without any broker configuration. Stored false keeps it off.
- With no `mqtt-enabled` key, MQTT defaults to enabled only when a valid stored
  broker configuration exists. Stored false keeps it off even after reconfiguration.
- `view_data_wifi_st.has_ip` means the station obtained an IP and gates MQTT,
  Native, and mDNS. Losing IP stops those LAN sessions.
- `view_data_wifi_st.is_network` is only the public-Internet ping result. A WAN
  outage must not disable LAN transports or local UI state.

Native listens on TCP 6053 when enabled and `has_ip` is true. It advertises
`_esphomelib._tcp.local.` through mDNS with a stable base-MAC-derived identity.
If mDNS is unavailable or discovery fails, HA can still add the device manually
by IP address or resolvable hostname and port 6053. An mDNS failure does not stop
the IP listener.

## Native security boundary

With no configured PSK—the default—Native API is plaintext and unauthenticated.
Any client that can reach port 6053 can read states and issue control commands.
Use only a trusted/isolated LAN or configure a unique PSK; MQTT TLS does not
encrypt Native traffic.

Configure the same random 32-byte base64 PSK on the device and in HA. Once a PSK
is configured, Native requires Noise NNpsk0 (Curve25519, ChaChaPoly, SHA-256) and rejects
plaintext instead of silently downgrading. `native psk clear` deliberately
returns a running Native service to plaintext.

The PSK is a fixed shared credential, not a device public-key identity. There is
no dynamic provisioning, automatic rotation, per-client authorization, or
flash/NVS encryption. NVS and the unauthenticated serial console require physical
access control. Commands and status output redact the PSK, but the person entering
it must still protect the console session.

## Console commands

```text
native status
native enable
native disable
native psk <32-byte-key-as-base64>
native psk clear

mqtt status
mqtt enable
mqtt disable
```

Existing `haconfig`, `mqtthelp`, `setmqtt`, and `setmqttca` commands configure only
MQTT. `haconfig` does not show Native status; use `native status`.

## Verification boundaries

Host checks exercise real Core, configuration, framing, Noise wire-format, and
UI synchronization code. Descriptor regeneration and nanopb roundtrip checks use
the optional pinned dependencies/runtime and report skips when those are absent:

```bash
python3 scripts/test_entity_core.py
python3 scripts/test_ha_transport_config.py
python3 scripts/test_native_api_protocol.py
python3 scripts/test_native_api_noise.py
python3 scripts/test_ha_switch_sync.py
python3 scripts/test_ha_switch_protocol.py
```

`scripts/test_native_api_integration.py` separately uses the pinned
`aioesphomeapi` 46.6.0 client against a supplied device/endpoint:

```bash
# Install scripts/requirements-native-api-integration.txt in an isolated env first.
python3 scripts/test_native_api_integration.py --host <device-ip>
python3 scripts/test_native_api_integration.py --host <device-ip> \
  --psk <base64-psk> --negative-tests
```

That client check covers reconnect, entity enumeration/snapshots, commands,
sensor push, and optional wrong-PSK/plaintext rejection. It does not itself test
HA discovery/UI. The [2026-11-03 current acceptance record](../../docs/stability/hardware-smoke-checklist.md#current-nativeha-acceptance-result-2026-11-03)
separates these 46.6.0 client passes from real Home Assistant Container 2026.9.1
(using `aioesphomeapi` 46.2.0) manual-IP onboarding and switch/number control.
It also records device mDNS, MQTT↔Native, lifecycle, fixed-issue, clean-build,
and remaining untested results; do not infer unlisted HA/hardware passes.

## Existing external-sensor limitation

`VIEW_EVENT_HA_SENSOR` still has no view consumer. MQTT external HA sensor values
are not the four local Core sensors, and the existing external-sensor cards remain
`N/A` until that separate UI path is implemented.
