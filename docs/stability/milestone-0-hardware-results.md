# Milestone 0 hardware validation result

Hardware validation was executed on 2026-10-27 against the ESP-IDF v5.5.6
baseline. The overall result is **PASS WITH OPEN ITEMS**.

This run validated the existing firmware behavior only. It did not modify or
flash the RP2040, change BSP/display/touch drivers, tune buses or DMA, alter the
wire protocol, change partitions, or start Milestone 1.

## Test record

| Field | Value |
| --- | --- |
| Date / tester | 2026-10-27 / user-assisted physical verification |
| ESP32-S3 branch | `idf55-stability-baseline` |
| ESP32-S3 build commit | `2da1c85` |
| ESP-IDF | `v5.5.6` / `880a203ae603d08a94d06aa01e0d6761958e5e9e` |
| Firmware version | `v1.1.0` |
| Target hardware | ESP32-S3 rev 0.1, 8 MiB flash, 8 MiB PSRAM |
| ESP32-S3 serial port | `/dev/ttyUSB0` (CH340) |
| RP2040 serial port | `/dev/ttyACM0` |
| RP2040 firmware identity | Not recorded; the attached RP2040 was not modified |
| Board serial / revision | Not recorded |
| Power source | USB during validation |
| Wi-Fi | `SEEED-COM`, WPA2, DHCP address `10.8.0.208` |
| MQTT | Mosquitto 2.1.2 at `mqtt://10.8.0.233:1883`, temporary anonymous test broker |
| Home Assistant | Protocol compatibility tested with direct MQTT capture; no HA server version recorded |

No credentials are included in this record.

## Validated artifacts

The ESP32-S3 was flashed successfully, and esptool verified the written image
hashes.

| Artifact | Size | SHA-256 |
| --- | ---: | --- |
| `indicator_ha.bin` | 1,779,264 bytes | `f14be01179076a59bc62eaf0af15b33c54f453385fa312fccf364621adb7783f` |
| `bootloader/bootloader.bin` | 21,904 bytes | `d83ac08a431f179a0b07925e844befab5514f64899c89c26da98dc0add25aacd` |
| `partition-table.bin` | — | `a7dfdccd649702098427a73dbc716db3ff9e51753271738f1d9d1f9ba0a5c67d` |

## Results

### Flash, boot, and runtime stability — PASS

- The ESP32-S3 image flashed and booted successfully.
- The boot log reported ESP-IDF v5.5.6 and firmware v1.1.0.
- Board, display, LVGL, touch, view, and model initialization completed.
- No panic, watchdog, assertion, allocation failure, or reset loop occurred.
- Warm restart, sleep/wake restart, and factory-reset restart completed cleanly.
- Sensor and UI processing continued after the final factory-reset restart.

### LCD, navigation, and touch — PASS

The user physically confirmed normal behavior for all three pages, navigation,
touch alignment, representative toggle/arc/slider controls, and the Wi-Fi,
Display, and MQTT dialogs. No blank screen, persistent corruption, touch
misalignment, UI stall, or reset was observed.

### Backlight and sleep/wake — PASS

- UI brightness changes were visible and logs showed changes through
  62, 38, 27, 20, 60, 96, and 100.
- The selected brightness persisted across restart before the factory reset.
- A roughly four-second hold posted `VIEW_EVENT_SHUTDOWN`, stopped Wi-Fi, and
  sent the RP2040 shutdown event.
- A short click then produced a clean software restart.
- Display, Wi-Fi, MQTT, configuration, and control state restored according to
  the current product behavior.
- The user confirmed sleep behavior was normal.

### RP2040 UART ingress — PASS WITH OPEN COVERAGE

- The attached RP2040 remained present at `/dev/ttyACM0` and was not flashed.
- Sensor events continued at roughly five-second intervals before and after
  restarts, demonstrating live RP2040-to-ESP32-S3 ingress under the existing
  firmware.
- CO2 and TVOC data reached the MQTT sensor path.
- The sleep path sent the current RP2040 shutdown event.

Hardware coverage did not include a raw byte-level capture of every packet
type. Temperature, humidity, and dynamic Grove `0xB8`–`0xBA` packets were not
observed with the attached RP2040/sensor setup.

### Wi-Fi — PASS

- With no saved configuration, the Wi-Fi setup path opened and scanning
  populated visible access points.
- The device connected to a WPA2 network and received `10.8.0.208`.
- The reachability check to `1.1.1.1` completed with 5/5 replies and 0% loss.
- Restart restored the saved connection before the final factory reset.
- Reconnection remained functional during the MQTT interruption tests.

An open AP and a WPA3-only AP were not available for this run.

### MQTT lifecycle — PASS

- The device connected to the temporary plaintext MQTT broker, subscribed to
  the switch command topic, and published sensor/control state.
- Three broker stop/start interruption cycles completed with reconnect and no
  device reset or UI lockup.
- The current broker configuration path was exercised through the UI.
- After factory reset, the erased MQTT configuration prevented the client from
  starting, as expected from the current implementation.

TLS transport was not tested.

### Home Assistant topic and payload compatibility — PASS WITH OPEN ITEMS

Observed device sensor payloads included:

```json
{"tvoc":"13"}
{"co2":"1170"}
```

All eight switch keys were received and applied. One exact round trip was:

```text
set:   indicator/switch/set   {"switch1":1}
state: indicator/switch/state {"switch1": 1}
```

Persisted switch values restored after a normal restart. The current
implementation also produced state echo and repeated intermediate
slider/arc updates; these are recorded below rather than changed in Milestone
0.

Temperature and humidity payloads were not observed because those sensor
values were not produced by the attached setup.

### Factory reset — PASS

The user completed and confirmed the physical factory reset. Serial evidence
showed:

```text
btn: factory reset
display: cfg not find
ha-config: mqtt broker cfg not find
wifi-model: Not config wifi, Entry wifi config screen
```

The reset erased saved Wi-Fi, display, MQTT/HA, and switch configuration,
restarted the ESP32-S3, returned controls to compiled defaults, opened Wi-Fi
setup, and resumed display/LVGL/RP2040 sensor processing. The RP2040 itself was
not erased or reflashed.

## Open items and follow-up debt

These items do not invalidate the v5.5.6 hardware baseline, but should be
addressed by scoped Milestone 1 work:

1. Wi-Fi and MQTT passwords are printed in plaintext logs. Redact credential
   fields before broader log capture or distribution.
2. Missing HA NVS configuration fills defaults internally but returns
   `ESP_ERR_NVS_NOT_FOUND`; MQTT therefore does not start until configuration
   is explicitly saved.
3. Programmatic HA widget synchronization emits
   `LV_EVENT_VALUE_CHANGED`, creating state echo.
4. Arc and slider drags publish and persist every intermediate value. Add a
   deliberate coalescing/debounce policy before changing behavior.
5. A long press produced duplicate sleep/shutdown postings. No crash or visible
   malfunction occurred, but the event source should be made idempotent.
6. ESP-IDF reports legacy I2C and ADC driver warnings, and flash HPM warnings
   were present.
7. Temperature, humidity, and dynamic Grove packet paths did not receive live
   hardware coverage in this setup.
8. Continuous live UART/UI observation reached approximately 13 minutes, not
   the checklist's requested 30-minute soak.
9. MQTT was validated with plaintext transport only; TLS was not exercised.
10. The RP2040 firmware commit and board serial/revision were not available for
    this record.

## Test-host cleanup

The temporary Mosquitto broker, MQTT subscriber, and ESP-IDF monitor were
stopped. The temporary broker directory was removed, and the temporary UFW
rule allowing LAN access to TCP port 1883 with comment
`indicator-hw-smoke` was deleted and verified absent.

## Decision

**Milestone 0: PASS WITH OPEN ITEMS.**

ESP-IDF v5.5.6 is accepted as the current implementation baseline for this
hardware. Milestone 1 may begin only as a separately scoped change set; the
open items above are not authorization to alter working display, touch,
RP2040, partition, or timing behavior without targeted evidence and tests.
