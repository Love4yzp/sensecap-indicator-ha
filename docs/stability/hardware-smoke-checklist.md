# Hardware smoke checklist

Use this checklist for the ESP-IDF v5.5.6 Milestone 1 stage-1 image. Record the device
revision, ESP32-S3/RP2040 firmware commits, tester, date, serial port, and any
attached Grove hardware before testing.

Do not change BSP, LCD/touch bus, DMA/timing, RP2040 protocol, partitions, or
eFuse settings while executing this checklist. A failure is evidence for a
separate scoped investigation, not permission to tune drivers in this stage.

## Test record

| Field | Value |
| --- | --- |
| Date / tester | |
| Board revision / serial | |
| ESP32-S3 commit | |
| ESP-IDF tag / commit | `v5.5.6` / `880a203ae603d08a94d06aa01e0d6761958e5e9e` |
| RP2040 firmware commit | |
| Power source | |
| Wi-Fi AP / security mode | |
| MQTT broker / transport | |
| Home Assistant version | |

## 1. Flash and boot

- [ ] Flash the generated bootloader, partition table, and
  `indicator_ha.bin` using the offsets printed by `./dev build`.
- [ ] Cold boot from power-off and capture the complete serial log.
- [ ] Confirm the boot banner reports firmware `v1.1.0`.
- [ ] Confirm initialization reaches board, LVGL, view, then model startup
  without reset loops, watchdogs, assertions, or allocation failures.
- [ ] Warm-restart the ESP32-S3 and confirm the same result.
- [ ] Leave the device idle for at least 10 minutes and confirm the UI remains
  responsive.

Expected source order:

```text
bsp_board_init
lv_port_init
view_event_init()
indicator_view_init
indicator_model_init
```

Model initialization then starts NVS, button, display, RP2040 UART/sensors,
command console, Wi-Fi, MQTT, and Home Assistant logic.

## 2. LCD, navigation, and touch

- [ ] Confirm the 480×480 LCD initializes with no blank frame, tearing,
  persistent corruption, or watchdog reset.
- [ ] Swipe through all three horizontal tiles: HA data, HA controls, and HA
  mixed controls.
- [ ] Open and close Wi-Fi, display, and MQTT modal screens.
- [ ] Exercise representative buttons, switch controls, sliders/arcs, text
  input, and back navigation.
- [ ] Confirm touches align with rendered controls across all screen edges.
- [ ] Repeat rapid navigation for at least two minutes and confirm the display
  continues refreshing.
- [ ] Exercise rapid UI input while Wi-Fi scan/results and sensor updates are
  active; confirm queue pressure does not freeze rendering or touch callbacks.

## 3. Backlight and sleep/wake

- [ ] On erased NVS, confirm the default brightness is 80.
- [ ] Change brightness through the UI and confirm smooth visible changes.
- [ ] Reboot and confirm the stored brightness is restored.
- [ ] Enable the display sleep timer and confirm the backlight turns off at the
  configured time.
- [ ] Single-click the user button to toggle display on/off.
- [ ] Hold for at least 3 seconds but less than 10 seconds, release, and confirm
  sleep behavior: display off, Wi-Fi stopped, and RP2040 shutdown command sent.
- [ ] Repeat the long press and confirm exactly one shutdown is handled per
  press; no duplicate Wi-Fi stop or RP2040 shutdown is logged.
- [ ] If a rejected shutdown can be induced under queue pressure, confirm the
  display returns on, the device remains awake, and a later release can retry.
- [ ] Use the current short-press wake path and confirm the device
  restarts cleanly.

The current backlight model clamps active brightness to 1–99 and drives GPIO
45 through LEDC. Do not tune that hardware path in this milestone.

## 4. RP2040 UART ingress

- [ ] Confirm UART2 communication at 115200 baud on ESP32-S3 TX GPIO19 / RX
  GPIO20.
- [ ] Confirm boot sends `PKT_TYPE_CMD_POWER_ON` (`0xA4`) to the RP2040.
- [ ] Confirm COBS frames are delimited by `0x00` and fragmented reads are
  reassembled.
- [ ] Observe valid legacy sensor updates for:
  - [ ] SCD41 CO2 (`0xB2` + native 32-bit float)
  - [ ] SHT41 temperature (`0xB3` + native 32-bit float)
  - [ ] SHT41 humidity (`0xB4` + native 32-bit float)
  - [ ] SGP40 TVOC index (`0xB5` + native 32-bit float)
- [ ] If dynamic Grove packets are available, verify attached (`0xB8`),
  detached (`0xB9`), and value (`0xBA`) frames pass their current XOR/length
  checks.
- [ ] Run continuous sensor input for at least 30 minutes and confirm no
  malformed-frame flood, UI stall, or reset.

Known compatibility risk to record, not fix here: dynamic packet IDs
`0xB8`–`0xBA` overlap legacy SEN5x IDs. Capture the actual RP2040 firmware and
observed frame types with the result.

## 5. Wi-Fi

- [ ] With erased Wi-Fi configuration, confirm boot opens the Wi-Fi
  configuration screen.
- [ ] Scan and confirm visible APs, RSSI ordering/values, and duplicate SSID
  handling are usable.
- [ ] Connect to an open AP if available.
- [ ] Connect to a WPA2/WPA3-compatible AP and obtain an IP address.
- [ ] Confirm the UI reports connected and network-reachable state after the
  current ping check to `1.1.1.1`.
- [ ] Reboot and confirm the ESP-IDF-stored Wi-Fi configuration reconnects.
- [ ] Power-cycle or disable the AP, then restore it; confirm current retry and
  reconnect behavior without a reset loop.
- [ ] After sleep/wake or a network interruption, confirm Wi-Fi/MQTT recovery
  while continuous RP2040 sensor input remains visible.
- [ ] Delete Wi-Fi configuration through the UI and confirm the stored station
  configuration is cleared.

## 6. MQTT lifecycle

- [ ] With network reachability true, confirm the configured MQTT client starts
  and reaches `MQTT_EVENT_CONNECTED`.
- [ ] Confirm disconnect and reconnect events are visible when the broker or
  network is interrupted.
- [ ] Change the broker address through the current UI and confirm the client
  stops, applies the URI, and starts again.
- [ ] Confirm the device remains responsive through at least three broker or
  network interruption cycles.
- [ ] Record whether plaintext `mqtt://` or TLS `mqtts://` was tested. The
  current default configuration is plaintext.

Do not publish captured credentials in the test record. Credential logging and
default broker credentials are Milestone 1 work.

## 7. Home Assistant topic and payload compatibility

Use a broker capture or Home Assistant MQTT listener and confirm these exact
current contracts:

| Direction | Topic | QoS | Payload examples |
| --- | --- | --- | --- |
| Device → broker | `indicator/sensor` | publish QoS 0 | `{"co2":"800"}`, `{"tvoc":"42"}`, `{"temp":"23.4"}`, `{"humidity":"55"}` |
| Broker → device | `indicator/switch/set` | subscribe QoS 1 | `{"switch1":1}` through `{"switch8":1}`; numeric value |
| Device → broker | `indicator/switch/state` | publish QoS 0 | `{"switch1": 1}` through `{"switch8": 1}` |

- [ ] Confirm each built-in sensor uses the exact topic, key, quoting, and
  numeric formatting above.
- [ ] Send switch set payloads for all eight keys and confirm the expected
  widget changes.
- [ ] Operate all eight switch widgets and confirm exactly one current state
  payload per user action.
- [ ] Reboot and confirm the current persisted switch state is restored.
- [ ] Record any duplicate publish/state echo separately; do not fix it in
  Milestone 0.

The generic HA sensor receive event currently has no view consumer. Do not mark
that unimplemented display path as a v5.5.6 regression.

## 8. Factory reset

This test is destructive: it erases the complete default NVS partition,
including Wi-Fi and application configuration.

- [ ] Before reset, configure Wi-Fi, display settings, MQTT/HA settings, and at
  least one persisted switch state.
- [ ] Hold the user button for at least 10 seconds.
- [ ] Confirm the log reports `factory reset`, the display turns on, and the
  device waits approximately 3 seconds.
- [ ] Confirm NVS is erased and the ESP32-S3 restarts.
- [ ] Confirm Wi-Fi configuration is absent and the Wi-Fi setup screen opens.
- [ ] Confirm display settings return to brightness 80 with sleep disabled.
- [ ] Confirm persisted switch state and MQTT/HA configuration return to their
  current compiled defaults.
- [ ] Confirm the RP2040 and display still initialize after reset.

## Result

- [ ] PASS: all required checks passed.
- [ ] PASS WITH OPEN ITEMS: list each unrun or accepted item below.
- [ ] FAIL: attach serial logs and the exact first failing step.

Open items / evidence:

```text

```
