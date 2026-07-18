# SenseCAP Indicator — Home Assistant Firmware

Firmware that turns the [Seeed Studio SenseCAP Indicator](https://www.seeedstudio.com/SenseCAP-Indicator-D1-p-5643.html) into a Home Assistant companion panel. Connects to Wi-Fi, publishes built-in sensor data over MQTT, and renders on-screen controls that Home Assistant can drive both ways.

<figure class="third">
    <img align="left" src="./assets/Home Assistant Data.png" width="240"/>
    <img align="center" src="./assets/Home Assistant.png" width="240"/>
    <img align="left" src="./assets/Home Assistant Control(ON).png" width="240"/>
    <img align="center" src="./assets/mqtt-address-panel.png" width="240"/>
</figure>


---

## Table of Contents

- [Features](#features)
- [Quick Start](#quick-start)
- [Architecture](#architecture)
- [MQTT Protocol](#mqtt-protocol)
- [Home Assistant Setup](#home-assistant-setup)
- [Build & Flash](#build--flash)
- [Console Commands](#console-commands)
- [Configuration](#configuration)
- [Development](#development)
- [Version](#version)

---

## Features

- [x] MQTT broker integration (configurable address, port, client ID, username, password)
- [x] Built-in sensor publishing: temperature, humidity, CO₂ (SCD41), tVOC (SGP40/SHT41)
- [x] Home Assistant control widgets: 6 binary switches + 2 numeric sliders
- [x] Wi-Fi and MQTT configuration from the touchscreen
- [x] MQTT broker configuration from the serial console (`setmqtt`, `mqtthelp`)
- [x] NVS-backed persistence for Wi-Fi credentials, MQTT config, switch state
- [x] Display brightness and sleep-mode controls
- [x] PC simulator for iterating on the UI without flashing hardware
- [ ] REST API
- [ ] WebSocket

---

## Quick Start

**Prerequisites:** ESP-IDF v5.4.x installed with `IDF_PATH` exported in your shell (see [ESP-IDF setup](https://docs.espressif.com/projects/esp-idf/en/v5.4/esp32s3/get-started/)).

```bash
git clone <this-repo>
cd sensecap-indicator-ha
./dev build && ./dev flash
./dev monitor          # Ctrl-] to exit
```

After flashing, configure Wi-Fi and MQTT broker on the device, then continue to [Home Assistant Setup](#home-assistant-setup). Both can also be set from the serial console — see [Console Commands](#console-commands).

---

## Architecture

The SenseCAP Indicator is a dual-MCU device:

| MCU | Role | Key resources |
|-----|------|---------------|
| **ESP32-S3** | Display, touch, Wi-Fi, MQTT, Home Assistant logic | 8 MB flash, PSRAM @ 120 MHz OCT mode, 240 MHz CPU |
| **RP2040** | Sensor acquisition on Grove ports | Reads CO₂, tVOC, temperature, humidity; relays to ESP32-S3 over UART |

The two chips communicate over a COBS-framed UART link. All Grove sensor access goes through the RP2040; the ESP32-S3 never reads sensors directly.

```
┌─────────────────────────────────────────────────────┐
│                     ESP32-S3                         │
│                                                      │
│  ┌──────────┐   ESP event   ┌──────────┐            │
│  │  Model   │◄─────loop────►│   View   │            │
│  │ (state,  │               │ (LVGL,   │            │
│  │  NVS,    │               │  touch   │            │
│  │  MQTT)   │               │  input)  │            │
│  └────┬─────┘               └──────────┘            │
│       │ UART / COBS                                  │
└───────┼─────────────────────────────────────────────┘
        │
┌───────┼─────────────────────────────────────────────┐
│       ▼          RP2040                              │
│  Packet dispatch                                     │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐    │
│  │   SCD41    │  │   SGP40    │  │   SHT41    │    │
│  │   (CO₂)    │  │   (tVOC)   │  │ (temp/hum) │    │
│  └────────────┘  └────────────┘  └────────────┘    │
└─────────────────────────────────────────────────────┘
```

### Model / View pattern

Each application domain has a paired `*_model.c` and `*_view.c`:

- **Model** — owns state, NVS reads/writes, MQTT publish/subscribe, RP2040 packet parsing
- **View** — owns LVGL object updates, touch callbacks, screen transitions

Model and view communicate exclusively through the **ESP event loop** with typed event IDs and payloads defined in `main/view_data.h`. Neither side calls the other's functions directly.

SquareLine Studio-generated files in `main/ui/` are treated as assets. Custom logic goes in `*_view.c` files, not in the generated screens.

For per-domain locations and architecture rules, see [`AGENTS.md`](AGENTS.md).

---

## MQTT Protocol

Three fixed topics carry all communication:

| Direction | Topic | Example payload |
|-----------|-------|-----------------|
| Device → HA (sensors) | `indicator/sensor` | `{"temp":"23.5","humidity":"45","co2":"450","tvoc":"100"}` |
| HA → Device (commands) | `indicator/switch/set` | `{"switch1":1,"switch5":50}` |
| Device → HA (state echo) | `indicator/switch/state` | `{"switch1":1,"switch2":0}` |

**Sensor keys:** `temp`, `humidity`, `co2`, `tvoc`

**Control keys:**

| Key | HA entity type | Range |
|-----|---------------|-------|
| `switch1`–`switch4`, `switch6`–`switch7` | Binary switch | `0` / `1` |
| `switch5`, `switch8` | Numeric slider | integer value |

---

## Home Assistant Setup

1. **Install an MQTT broker** (Mosquitto is the simplest) and enable the MQTT integration in Home Assistant.

2. **Point the Indicator at the broker** — from the device's MQTT screen, or via [`setmqtt`](#console-commands) over serial.

3. **Add MQTT entities.** Append [`examples/homeassistant/mqtt-entities.yaml`](examples/homeassistant/mqtt-entities.yaml) to your `configuration.yaml` under the `mqtt:` key, then reload MQTT.

4. **Add the dashboard.** Paste [`examples/homeassistant/dashboard.yaml`](examples/homeassistant/dashboard.yaml) into the Lovelace Raw Configuration Editor.

<img src="./assets/Home Assistant Dashboard.png" />

See also: [Seeed wiki — Home Assistant application guide](https://wiki.seeedstudio.com/SenseCAP_Indicator_Application_Home_Assistant/).

---

## Build & Flash

### ESP32-S3 (main firmware)

**Prerequisites:** ESP-IDF v5.4.x with `IDF_PATH` exported.

```bash
./dev build          # idf.py build
./dev flash          # auto-detects port
./dev monitor        # Ctrl-] to exit
./dev fullclean      # reset build directory
```

Manual `idf.py` works the same way:

```bash
. "$IDF_PATH/export.sh"
idf.py build
idf.py -p /dev/ttyUSB0 -b 460800 flash monitor
```

**Critical `sdkconfig.defaults` settings — do not remove:**

- `CONFIG_LV_MEM_CUSTOM=y` — required; removing causes LVGL to freeze
- PSRAM clock: 120 MHz OCT mode
- CPU: 240 MHz, flash: QIO 120 MHz

App partition is 7 MB (`partitions.csv`); total flash is 8 MB.

### RP2040 (sensor coprocessor)

Only needed when changing sensor protocols. Current version is **v2.1.0**. Built with PlatformIO (Arduino core by Earle Philhower).

```bash
./dev rp2040 build
./dev rp2040 upload      # device must appear as /dev/cu.usbmodem*
./dev rp2040 monitor
```

---

## Console Commands

Connect at the device's baud rate and use these commands:

| Command | Description |
|---------|-------------|
| `mqtthelp` | Print broker, topic, and payload examples |
| `haconfig` | Print the current MQTT/HA configuration |
| `setmqtt -a <addr>` | Set broker address (e.g., `mqtt://192.168.1.10:1883`) |
| `setmqtt -a <addr> -c <client-id> -u <user> -p <pass>` | Full broker configuration |

**Examples:**

```text
setmqtt -a 192.168.1.10 -c indicator-01 -u mqtt_user -p mqtt_password
setmqtt --addr mqtt://192.168.1.10:1883
setmqtt --addr mqtt://broker.emqx.io
```

After `setmqtt` succeeds, the configuration is saved to NVS and the MQTT client restarts automatically.

### Security notes

MQTT and Wi-Fi credentials are stored in plaintext in NVS, and the serial console is unauthenticated — anyone with physical or USB access can read them back. Use a dedicated MQTT user with a limited ACL so a leaked credential can't reach the rest of your broker. Before reselling or disposing of the device, run `idf.py erase-flash` to wipe stored credentials.

---

## Configuration

Run `idf.py menuconfig` to explore options. Notable locations:

- **LVGL fonts** — `Component config → LVGL configuration → Font usage`
- **PSRAM clock** — `Components → ESP PSRAM → SPI RAM config` (requires "Make experimental features visible")

To regenerate `sdkconfig` from defaults: delete it and run `./dev build`.

---

## Development

**Code completion.** Install [clangd](https://github.com/clangd/clangd/releases) and the [clangd VS Code extension](https://marketplace.visualstudio.com/items?itemName=llvm-vs-code-extensions.vscode-clangd). After one successful build, `build/compile_commands.json` is generated and clangd uses it automatically for ESP-IDF-aware navigation.

**PC simulator.** The `sim/` directory builds a desktop version of the UI using LVGL's SDL2 backend (macOS, 480×480 window matching the real LCD). Useful for iterating on screens, fonts, and layouts without flashing.

```bash
cmake -S sim -B sim/build && cmake --build sim/build -j4
./sim/build/sensecap_sim
```

**Architecture rules.** See [`AGENTS.md`](AGENTS.md) for module locations, boot sequence, and the model/view boundary rules that contributors (human and AI) must follow.

---

## Version

| Component | Version |
|-----------|---------|
| Firmware (ESP32-S3) | `v1.1.0` |
| Coprocessor (RP2040) | `v2.1.0` |
| ESP-IDF | `v5.4.x` |
| LVGL | `v9.x` (managed component) |
| RP2040 build system | PlatformIO |

<details>
<summary>Changelog</summary>

| Version | Notes |
|---------|-------|
| `v1.1.0` | Upgraded to ESP-IDF 5.4.x and LVGL 9; added `mqtthelp` console command; improved MQTT setup UX; unified build/flash tooling into `./dev` |
| `v1.0.0` | Initial release (ESP-IDF 5.1 era, LVGL 8) |

</details>
