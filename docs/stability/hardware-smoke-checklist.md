# Hardware smoke checklist

Use this checklist for the current ESP-IDF v5.5.6 image, including Entity Core,
independent MQTT/Native lifecycle, mDNS, and Noise PSK. Record the device
revision, ESP32-S3/RP2040 firmware commits, tester, date, serial port, and any
attached Grove hardware before testing.

The populated test record below is only the historical 2026-10-28 Milestone 1
smoke for the exact image hash listed there. It is not evidence for the current
Native API, dual-transport, real Home Assistant, or Noise implementation. The
separate [2026-11-03 current result](#current-nativeha-acceptance-result-2026-11-03)
records those later passes and the scenarios that remain untested.

Do not change BSP, LCD/touch bus, DMA/timing, RP2040 protocol, partitions, or
eFuse settings while executing this checklist. A failure is evidence for a
separate scoped investigation, not permission to tune drivers in this stage.

## Test record

| Field | Value |
| --- | --- |
| Date / tester | `2026-10-28` / user-confirmed manual smoke |
| Board revision / serial | |
| ESP32-S3 commit | Milestone 1 stage-1 image; exact binary recorded below |
| ESP-IDF tag / commit | `v5.5.6` / `880a203ae603d08a94d06aa01e0d6761958e5e9e` |
| Firmware image | `indicator_ha.bin` size `0x1b38a0`; SHA-256 `4ad845a073e64b162fcb49c3ac7e87580395cb175930107e08c98c7909b236e3` |
| Flash port | `/dev/ttyUSB0` (`1a86:7523`, CH340 / ESP32-S3) |
| RP2040 firmware commit | Not changed; `/dev/ttyACM0` (`2886:0050`) was not operated on |
| Power source | |
| Wi-Fi AP / security mode | |
| MQTT broker / transport | Not configured; session recovery not verified |
| Home Assistant version | Not configured; integration recovery not verified |

## 1. Flash and boot

- [ ] Flash the generated bootloader, partition table, and
  `indicator_ha.bin` using the offsets printed by `./dev build`.
- [ ] Cold boot from power-off and capture the complete serial log.
- [ ] Confirm the boot banner matches the `App "indicator_ha" version:` value
  from the build log and Native `DeviceInfo.project_version`. For an exact
  release-tag build, all three report that `vX.Y.Z` tag.
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

Model initialization then starts NVS, button, display, RP2040 UART/sensors, and
the command console; next it initializes Entity Core, HA/MQTT adapters, and
Native API. The shared MQTT lifecycle controller starts before Wi-Fi so early
GOT_IP events cannot strand a pending start. Core exists before either network
transport and is not reset by transport changes.

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
- [ ] If a Wi-Fi scan result cannot be delivered, confirm the spinner stops
  within 15 seconds and a later scan can be requested.
- [ ] If a Wi-Fi scan itself fails, confirm the spinner stops and the modal
  remains responsive for a later scan request.
- [ ] Close the Wi-Fi modal during a scan, reopen it immediately, and confirm
  an old success/failure cannot stop or discard the new scan.

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

## 5. Wi-Fi and LAN/WAN state

- [ ] With erased Wi-Fi configuration, confirm boot opens the Wi-Fi
  configuration screen.
- [ ] Scan and confirm visible APs, RSSI ordering/values, and duplicate SSID
  handling are usable.
- [ ] Connect to an open AP if available.
- [ ] Connect to a WPA2/WPA3-compatible AP and obtain an IP address.
- [ ] Confirm `has_ip` becomes true only after the IP event. Confirm the separate
  `is_network` state follows the current public ping check to `1.1.1.1`.
- [ ] Block WAN while preserving the LAN. Confirm `has_ip` remains true even if
  `is_network` becomes false, and enabled Native/MQTT LAN sessions remain usable.
- [ ] Reboot and confirm the ESP-IDF-stored Wi-Fi configuration reconnects.
- [ ] Power-cycle or disable the AP, then restore it; confirm current retry and
  reconnect behavior without a reset loop.
- [ ] Confirm losing IP stops Native/mDNS and MQTT sessions, and recovery starts
  the enabled transports without clearing Core or creating duplicate entities.
- [ ] After sleep/wake or a network interruption, confirm Wi-Fi/MQTT/Native
  recovery while continuous RP2040 sensor input remains visible.
- [ ] Delete Wi-Fi configuration through the UI and confirm the stored station
  configuration is cleared and `has_ip` is false.

## 6. Independent MQTT lifecycle

- [ ] On fresh NVS without a valid stored broker configuration, confirm MQTT
  stays disabled even though Native defaults to enabled.
- [ ] Store a valid broker configuration with no `mqtt-enabled` key and confirm
  MQTT defaults to enabled. Then run `mqtt disable`, change the configuration,
  and confirm the explicit disable remains effective.
- [ ] With `has_ip=true`, confirm the enabled/configured MQTT client starts and
  reaches `MQTT_EVENT_CONNECTED`; public `is_network` is not required for a LAN
  broker.
- [ ] Confirm disconnect and reconnect events are visible when the broker or
  network is interrupted.
- [ ] Change the broker address through the current UI and confirm the old client
  stops, the stored URI is applied, and a new client starts when enabled/LAN-ready.
- [ ] Use `mqtt status`, `mqtt enable`, and `mqtt disable`; confirm each changes
  only MQTT and leaves Native and Core state intact.
- [ ] Confirm the device remains responsive through at least three broker or
  network interruption cycles.
- [ ] Record whether plaintext `mqtt://` or TLS `mqtts://` was tested.

Do not publish captured credentials in the test record. Credential logging and
default broker credentials are Milestone 1 work.

## 7. Home Assistant MQTT topic and payload compatibility

Use a broker capture or Home Assistant MQTT listener and confirm these exact
current contracts:

| Direction | Topic | QoS | Payload examples |
| --- | --- | --- | --- |
| Device → broker | `indicator/sensor` | publish QoS 0 | `{"co2":"800"}`, `{"tvoc":"42"}`, `{"temp":"23.4"}`, `{"humidity":"55"}` |
| Broker → device | `indicator/switch/set` | subscribe QoS 1 | `{"switch1":1}` through `{"switch8":1}`; numeric value |
| Device → broker | `indicator/switch/state` | publish QoS 0 | `{"switch1": 1}` through `{"switch8": 1}` |

- [ ] Confirm each built-in sensor uses the exact topic, key, quoting, and
  numeric formatting above.
- [ ] Send set payloads for all eight legacy keys and confirm the expected Core
  value/widget changes, including numeric slots 5 and 8.
- [ ] Operate all eight control slots and confirm one current MQTT state payload
  per accepted local command while connected, after debounce for arc/slider input.
- [ ] Reboot and confirm all controls cold-start at zero instead of restoring
  runtime state from NVS; sensors remain missing until fresh RP2040 data arrives.
- [ ] Disconnect the broker, operate local controls, and confirm accepted values
  remain visible/effective without MQTT-driven rollback. Reconnect and confirm
  the latest Core snapshots publish.
- [ ] Record any duplicate publish/state echo separately.

The generic external HA sensor receive event currently has no view consumer and
is separate from the four local Core sensors. Do not mark that unimplemented
display path as a v5.5.6 regression.

## 8. Native API and real Home Assistant acceptance

This is the reusable real-environment matrix. The current result below records
which scenarios were actually covered; unchecked or unlisted scenarios are not
claimed. Host tests and a host-side `aioesphomeapi` client run do not by
themselves satisfy real-HA checks.

- [ ] On fresh NVS with Wi-Fi but no MQTT broker configuration, run
  `native status` and confirm Native is enabled, has an IP, and listens on 6053.
- [ ] In real Home Assistant, confirm mDNS `_esphomelib._tcp.local.` discovery
  selects the stable MAC-suffixed device without duplicate entities.
- [ ] Disable/bypass mDNS discovery and add ESPHome manually by IP; repeat with a
  resolvable hostname if supported by the test LAN. Use port 6053.
- [ ] Confirm exactly 4 sensors, 6 switches, and 2 numbers enumerate with stable
  identities. Verify initial snapshots, sensor `missing_state`, and subsequent
  RP2040 sensor pushes.
- [ ] Operate controls from touch, HA Native, and an external MQTT client; confirm
  every other endpoint receives the same authoritative Core state without loops.
- [ ] Verify number min/max/step, invalid/non-finite command rejection, and the
  existing 200 ms arc/slider debounce without LVGL stalls.
- [ ] Disconnect HA and then the broker independently. Confirm accepted local
  values do not roll back, the other transport remains functional, and reconnect
  sends current Core snapshots.
- [ ] Exercise both off, MQTT only, Native only, and both on using the real
  `mqtt`/`native` console commands. Confirm transport switches do not restart the
  peer or clear Core.
- [ ] Restart HA, reboot the device, and restart the broker; confirm recovery,
  zero cold-start controls, fresh sensor state, and no zombie/duplicate sessions.
- [ ] Record dual-transport idle/connected/traffic heap, task stack headroom,
  socket count, and LVGL responsiveness; establish thresholds only from evidence.

### Fixed Noise PSK security checks

- [ ] Before setting a PSK, record that Native is plaintext and unauthenticated;
  perform this exposure test only on a trusted/isolated LAN.
- [ ] Generate a unique random 32-byte key, configure it with
  `native psk <32-byte-key-as-base64>`, and configure the same key in real HA.
  Confirm command results, logs, and `native status` do not print the complete key.
- [ ] Confirm a correct PSK completes Noise and all entity/command flows work.
- [ ] Confirm a wrong PSK and a plaintext connection are rejected while a PSK is
  configured; there must be no silent plaintext downgrade.
- [ ] Confirm `native psk clear` deliberately restores plaintext and document the
  resulting exposure. Treat NVS/flash and the unauthenticated serial console as
  outside the encryption boundary; dynamic provisioning/rotation is not present.

### Host-side aioesphomeapi result (record separately)

After installing `scripts/requirements-native-api-integration.txt` in an isolated
environment, the optional client-level checks are:

```bash
python3 scripts/test_native_api_integration.py --host <device-ip>
python3 scripts/test_native_api_integration.py --host <device-ip> \
  --psk <base64-psk> --negative-tests
```

Record the command, pinned client 46.6.0, endpoint image, and actual output in a
separate result field. These checks cover client reconnect, entity snapshots,
commands, sensor push, and optional wrong-key/plaintext rejection; they do not
cover mDNS or real HA UI and do not check any box above by themselves.

## 9. Factory reset

This test is destructive: it erases the complete default NVS partition,
including Wi-Fi and application configuration.

- [ ] Before reset, configure Wi-Fi, display, MQTT, and a Native PSK; change at
  least one runtime control value and record that it is not expected to persist.
- [ ] Hold the user button for at least 10 seconds.
- [ ] Confirm the log reports `factory reset`, the display turns on, and the
  device waits approximately 3 seconds.
- [ ] Confirm a failed compatibility notification does not prevent that local
  NVS erase/restart sequence, and only one reset timer starts per press.
- [ ] Confirm NVS is erased and the ESP32-S3 restarts.
- [ ] Confirm Wi-Fi configuration is absent and the Wi-Fi setup screen opens.
- [ ] Confirm display settings return to brightness 80 with sleep disabled.
- [ ] Confirm all controls cold-start at zero, stored MQTT configuration/PSK are
  absent, MQTT remains disabled without valid stored configuration, and the
  missing `native-enabled` key again defaults Native to enabled.
- [ ] Confirm the RP2040 and display still initialize after reset.

## Historical Milestone 1 result (2026-10-28 image only)

- [ ] PASS: all required checks passed.
- [x] PASS WITH OPEN ITEMS: list each unrun or accepted item below.
- [ ] FAIL: attach serial logs and the exact first failing step.

Open items / evidence:

```text
2026-10-28 manual smoke:
- Rapid swipes and repeated clicks completed without a freeze.
- Closing a Wi-Fi scan and immediately reopening it ended the spinner normally
  and displayed the new list.
- One long press produced one sleep transition; a short press woke the device.
- LCD, touch, Wi-Fi icon, Wi-Fi recovery, and continuous sensor values passed.
- ESP-IDF v5.5.6 startup monitoring showed no panic, watchdog, or assertion.
- Only /dev/ttyUSB0 ESP32-S3 was flashed; the RP2040 was not operated on.

Open integration check:
- MQTT/HA broker was not configured, so MQTT/HA session recovery is not
  claimed as verified.
```

## Current Native/HA acceptance result (2026-11-03)

This result is limited to the scenarios explicitly listed as passed below. It
does not convert the reusable checklist above into a complete pass.

### Environment and build

| Field | Current validated value |
| --- | --- |
| Device name | SenseCAP Indicator D81A08 |
| Device IP | 10.8.0.209 |
| Real Home Assistant | Home Assistant Container 2026.9.1, using `aioesphomeapi` 46.2.0 |
| Separate Native client | `aioesphomeapi` 46.6.0 |
| Firmware baseline | ESP-IDF v5.5.6 |
| Final build | Clean ESP32-S3 `./dev build` passed with ESP-IDF v5.5.6 |

### Passed in this validation

- Real Home Assistant manual-IP setup through **Settings → Devices & services →
  Add Integration → ESPHome**, using `10.8.0.209` and port 6053, completed
  successfully.
- The ESPHome device page showed all 12 entities: 4 sensors, 6 switches, and 2
  numbers. Switch and number API control from real Home Assistant passed.
- Device mDNS advertisement was verified on the actual hardware.
- Separate `aioesphomeapi` 46.6.0 client tests passed in plaintext and Noise
  modes. A wrong PSK was rejected, plaintext downgrade was rejected while a PSK
  was configured, and `native psk clear` restored plaintext operation.
- MQTT→Native and Native→MQTT state synchronization both passed.
- MQTT-only, Native-only, and dual-transport operation passed, as did the
  single-transport disable/re-enable recovery check.

### Issues found and fixed during validation

1. A NULL unit for a unitless entity caused a crash during entity enumeration;
   the unit handling was fixed.
2. Advertising `api_encryption_supported=true` without a configured PSK caused
   a dynamic key-provisioning timeout; the capability behavior was fixed.
3. A `project_name` without the required `author.project` form caused a Home
   Assistant registration exception; the project identity was fixed.

### Remaining untested / not claimed

- Home Assistant's automatic-discovery UI; the device mDNS pass is not a UI pass.
- Real Home Assistant using a PSK; the separate Noise client pass is not a
  real-HA PSK pass.
- Physical-touch offline operation and its local UI behavior.
- The both-off transport combination; the other three combinations are listed
  above.
- Home Assistant restart and broker restart/recovery.
- A WAN-only outage while the LAN remains available.
- Resource/heap/stack/socket measurements, defined performance thresholds, and
  long-duration stability.
