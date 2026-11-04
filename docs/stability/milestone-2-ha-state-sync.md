# Milestone 2: Home Assistant state synchronization

The HA control slice uses deterministic generation/debounce handling at the
existing LVGL seam and one authoritative Entity Core across touch, MQTT, and
Native API. The MQTT topic names, JSON keys, payload formats, and QoS remain
compatible; Native API is an additional transport, not MQTT Discovery.

## Entity authority

`main/ha/entity_registry.[ch]` defines stable identities and shared metadata for
4 local sensors, 6 switches, and 2 numbers. The numbers retain UI/MQTT control
slots 5 and 8 and their legacy `switch5`/`switch8` JSON keys.

`main/ha/entity_core.[ch]` exclusively owns current values, validity, source,
and `state_version`. It validates entity type, finite values, range, and step;
commits accepted commands; and notifies observers after releasing its lock.
Snapshots are copied under the lock. Registry/Core do not perform socket, MQTT,
or LVGL operations.

Control state is RAM-only and cold-starts at zero. Sensor state starts invalid
until the first valid RP2040 sample. No entity runtime state is restored from or
persisted to NVS. Enabling, disabling, reconnecting, or reconfiguring a transport
does not reset Core.

UI `generation` correlates one local operation. Core `state_version` identifies a
committed state. The two counters have different owners and are never interchanged.

## Interface and existing LVGL seam

`main/ha/ha_switch_sync.[ch]` remains a pure in-process module accepting:

- a discrete or debounced user value;
- expiration of the debounce window;
- a local Core command result tagged with the UI generation;
- an authoritative Core model value from boot, touch, MQTT, or Native.

It owns generation allocation, pending debounce, the last accepted value,
stale-result filtering, and local restoration decisions. The screen does not
reconstruct transport state.

`ha_switch_screen.c` remains the LVGL adapter and retains `switch_slot_t`,
`widget_update_fn`, `suppress_post`, and one-shot timers. `ha_switch.c` adapts
view events to Core commands and Core snapshots to UI/MQTT; it has no LVGL object
ownership. Native has its own dirty-state observer and bounded per-client queues.

## Event contract

| Event | Payload | Policy | Current meaning |
| --- | --- | --- | --- |
| `VIEW_EVENT_HA_SWITCH_ST` | `struct view_data_ha_switch_command` | Critical command | A generation-tagged local command ready for Core validation/commit. |
| `VIEW_EVENT_HA_SWITCH_SET` | `struct view_data_ha_switch_data` | Replaceable state keyed by control slot | The latest committed Core value from any source, including initial snapshots. |
| `VIEW_EVENT_HA_SWITCH_PUBLISH_RESULT` | `struct view_data_ha_switch_publish_result` | Critical command result | Legacy event name; reports Core acceptance and authoritative value, not ESP-MQTT publish acceptance. |

Core acceptance means the local virtual entity has changed. It does not mean a
broker, HA client, or external physical device has acknowledged anything. A
rejected Core command leaves the previous authoritative value intact and returns
that value to the UI seam.

## Runtime behavior

- Toggle widgets submit immediately. Arc and slider widgets restart a 200 ms
  LVGL one-shot timer and submit only the latest value after input becomes quiet.
- An accepted local command updates Core and notifies UI, Native, and MQTT
  observers. MQTT state is published only if that client is connected; Native
  marks dirty state only for subscribed clients.
- **MQTT or Home Assistant offline/publish failure does not roll back an accepted
  local command.** The virtual entity remains effective locally and later
  transports read the latest Core snapshot.
- A posted local command retains the existing 2 second local-result timeout.
  View-event rejection, timer-allocation failure, invalid Core input, or a missing
  Core result can restore the last accepted visible value. This is UI/event
  synchronization, not a broker/HA availability rule.
- A late result is generation-filtered. Committed model state cancels pending
  debounce/result timers, invalidates older local results, and is applied under
  the LVGL lock with `suppress_post` to prevent programmatic event echoes.
- MQTT and Native inputs use the same Core command validation as touch. Accepted
  remote input updates the UI and the other transport without treating a state
  report as a new command.
- HA/MQTT initialization publishes current Core control snapshots independently
  of a broker connection. MQTT subscribe/reconnect publishes current snapshots;
  Native `SubscribeStates` sends all 12 current snapshots, with missing sensors
  marked `missing_state`.
- The four built-in sensor paths update Core first. MQTT keeps its existing
  single-key JSON publications and Native uses the same authoritative snapshot.

## Independent transport and LAN behavior

MQTT and Native have independent enable NVS keys and lifecycle handlers. Either
can run alone or both can coexist; changing one does not restart the other or
clear Core. Fresh/missing `native-enabled` defaults Native to enabled, while an
explicit false keeps it disabled. Fresh MQTT defaults to enabled only when a
valid stored broker configuration exists.

Both use `view_data_wifi_st.has_ip` as the LAN-ready condition. `is_network` is
only the public-Internet ping result, so a WAN failure is not a reason to stop
local UI, MQTT, Native, or mDNS.

Native advertises `_esphomelib._tcp.local.` and also accepts manual IP/hostname
connections on port 6053. No PSK means plaintext, unauthenticated Native access;
a configured PSK requires Noise and rejects plaintext downgrade. See
`main/ha/README.md` for the fixed-PSK and storage/console security boundaries.

## Host verification

- `scripts/test_ha_switch_sync.py` compiles the pure C UI seam and covers latest
  debounced input, stale results, rejection restoration, and model cancellation.
- `scripts/test_entity_core.py` covers validation, RAM snapshots, source/version,
  observers, and local commit without a transport.
- `scripts/test_ha_transport_config.py` covers independent defaults, explicit
  disable, lifecycle idempotency, and `has_ip` versus `is_network`.
- `scripts/test_ha_switch_protocol.py` checks Core/UI responsibilities and the
  unchanged MQTT wire contract.
- `scripts/test_native_api_protocol.py` and `scripts/test_native_api_noise.py`
  cover real framing/protobuf/Noise wire-format code within their host stubs and
  optional pinned dependencies.

The separately invoked `scripts/test_native_api_integration.py` uses pinned
`aioesphomeapi` 46.6.0 against a supplied endpoint. Its entity/snapshot/command/
sensor/reconnect and optional wrong-PSK/plaintext checks are client-level results,
not Home Assistant UI, mDNS, or full hardware acceptance. Record them separately
from the matrix below.

## Real HA/hardware results and remaining boundary

Real-environment results are no longer wholly pending. The [2026-11-03 current
acceptance record](hardware-smoke-checklist.md#current-nativeha-acceptance-result-2026-11-03)
documents Home Assistant Container 2026.9.1 (`aioesphomeapi` 46.2.0) manual-IP
onboarding with all 12 entities, real-HA switch/number API control, device mDNS,
MQTT↔Native synchronization, single-transport disable/recovery, and separate
46.6.0 plaintext/Noise client checks including wrong-PSK and downgrade rejection.
It also records the three issues found and fixed during that validation and the
final ESP-IDF v5.5.6 clean build.

The following remain unverified and must not be inferred from those passes:

- Home Assistant's automatic-discovery UI (device mDNS was verified separately);
- real Home Assistant using a PSK (the Noise client check is a different path);
- physical-touch offline operation and transport-independent local rollback rules;
- the both-off transport combination;
- Home Assistant and broker restart/recovery;
- a WAN-only outage while LAN remains available;
- resource measurements and long-duration stability.

Continue using `docs/stability/hardware-smoke-checklist.md` for actual endpoint,
versions, image/build evidence, covered scenarios, and unrun items.
