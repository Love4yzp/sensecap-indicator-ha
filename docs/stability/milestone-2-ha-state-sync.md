# Milestone 2: Home Assistant switch synchronization

Milestone 2 makes the Home Assistant switch slice deterministic under rapid
local input, MQTT disconnects, delayed results, and broker-driven updates. It
builds on the Milestone 1 view-event seam without changing MQTT topic names or
JSON payload compatibility.

## Interface and seam

`main/ha/ha_switch_sync.[ch]` is a pure in-process module. Its interface accepts
four semantic transitions:

- a discrete or debounced user value;
- expiration of the debounce window;
- an MQTT publish result tagged with the local generation;
- an authoritative model value received from MQTT.

The implementation owns generation allocation, pending debounce state, the
last controller-confirmed value, stale-result rejection, and rollback choice.
The screen does not compare generations or reconstruct accepted state.

`ha_switch_screen.c` is the LVGL adapter at this seam. It owns the one-shot
timers and widget application. `ha_switch.c` is the MQTT controller and no
longer owns a screen pointer or calls LVGL.

## Event contract

| Event | Payload | Policy | Meaning |
| --- | --- | --- | --- |
| `VIEW_EVENT_HA_SWITCH_ST` | `struct view_data_ha_switch_command` | Critical command | A generation-tagged local command ready for MQTT publication. |
| `VIEW_EVENT_HA_SWITCH_SET` | `struct view_data_ha_switch_data` | Replaceable state keyed by switch index | The initial zero value or latest authoritative model value from MQTT. |
| `VIEW_EVENT_HA_SWITCH_PUBLISH_RESULT` | `struct view_data_ha_switch_publish_result` | Critical command result | Whether ESP-MQTT accepted the publish, plus the controller's authoritative rollback value. |

The result value is deliberately authoritative even on rejection. If an older
success result is lost and a later command fails, the screen still rolls back
to the controller's latest in-memory value rather than to stale visible input.

## Runtime behavior

- Toggle widgets publish immediately.
- Arc and slider widgets restart a 200 ms LVGL one-shot timer on each change;
  only the latest value is published after the input becomes quiet.
- A posted local command starts a 2 second result timeout. Immediate view-event
  rejection or timeout rolls back through the synchronization module. A late
  success for that same generation reapplies the controller-authoritative
  value; a newer user/model generation still masks it. If a newer local
  command is rejected before an older pending command reports success, the
  older confirmed controller value is reapplied.
- `esp_mqtt_client_publish()` is accepted only when ESP-IDF returns a
  non-negative message id. Disconnected, formatting, outbox-full, and publish
  errors produce a negative result.
- A successful local publish updates the controller's in-memory value before
  its result is emitted.
- The screen and controller cold-start all eight values at zero. Switch values
  are not restored from or persisted to NVS; HA/MQTT state is authoritative
  after startup.
- Initial zero state is published during HA model initialization. It does not
  depend on an MQTT broker connection or subscription completing.
- MQTT inbound values update in-memory controller state and publish keyed
  replaceable state.
- Model state cancels pending debounce and result timers, applies under the
  LVGL lock, and invalidates older local results.
- Programmatic widget application retains the slot-local suppression guard, so
  LVGL `VALUE_CHANGED` re-entry cannot create a state echo.

## Verification

`scripts/test_ha_switch_sync.py` compiles the pure C module on the host and
proves:

- repeated continuous input flushes only the latest value;
- an older result cannot overwrite newer visible input;
- rejection rolls back to the latest controller-authoritative value;
- model state cancels pending debounce and masks stale results.

`scripts/test_ha_switch_protocol.py` checks the event payload/category contract,
the controller's UI-free ownership, result publication, screen subscriptions,
timer policy, and suppression path. Both tests run through
`scripts/dev_check.py --skip-build`.

## Hardware boundary

The firmware still needs an end-to-end broker smoke with a configured MQTT/Home
Assistant endpoint. The required observations are:

- rapid arc/slider movement produces one final state publish per quiet period;
- a disconnected broker causes the control to return to the last accepted
  value within the result timeout;
- a broker command cancels a pending local debounce and becomes the visible
  value;
- reconnect and retained/set messages do not create duplicate outbound state
  echoes.
