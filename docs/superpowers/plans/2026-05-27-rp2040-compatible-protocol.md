# RP2040 Compatible Protocol Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a backward-compatible RP2040 sensor protocol path while preserving the existing Home Assistant sensor flow.

**Architecture:** Keep legacy `type + float` packets for current UI/MQTT consumers. Add dynamic packet constants and RP2040 emitter helpers for attach/detach/value frames, and harden the ESP32 UART/COBS receiver so fragmented frames are handled correctly.

**Tech Stack:** ESP-IDF C, Arduino RP2040 sketch/C++, PacketSerial/COBS, Python static verification.

**RP2040 build-system decision:** This plan documents the pre-`v2.0.1` Arduino
sketch phase. After tagging `v2.0.1`, the RP2040 firmware migrated to
PlatformIO with sources under `rp2040/src/` and headers under `rp2040/include/`.

---

### Task 1: Protocol Parity Test

**Files:**
- Create: `scripts/verify_rp2040_protocol.py`
- Modify: none

- [ ] **Step 1: Create a verifier that extracts `PKT_TYPE_*` and `PKT_SENSOR_*` constants from ESP32 and RP2040 headers.**
- [ ] **Step 2: Run it before protocol changes and confirm it fails because dynamic constants are missing.**
- [ ] **Step 3: Keep this script as the regression check for later tasks.**

### Task 2: Shared Packet Definitions

**Files:**
- Modify: `main/app/esp32_rp2040.h`
- Modify: `rp2040/include/indicator_rp2040.hpp`

- [ ] **Step 1: Add command constants for beep off and Grove rescan.**
- [ ] **Step 2: Add dynamic packet constants `SENSOR_ATTACHED`, `SENSOR_DETACHED`, `SENSOR_VALUE`.**
- [ ] **Step 3: Add reserved onboard sensor IDs and sensor categories.**
- [ ] **Step 4: Run protocol verifier and confirm both sides match.**

### Task 3: RP2040 Compatible Emitter

**Files:**
- Modify: `rp2040/include/indicator_rp2040.hpp`
- Modify: `rp2040/src/sensors.cpp`
- Modify: `rp2040/src/main.cpp`

- [ ] **Step 1: Add `sensor_attached_send`, `sensor_detached_send`, and `sensor_value_send`.**
- [ ] **Step 2: Keep `sensor_data_send` unchanged for legacy packets.**
- [ ] **Step 3: Emit dynamic attached packets during setup for onboard sensors.**
- [ ] **Step 4: Emit dynamic value packets next to each legacy value packet.**
- [ ] **Step 5: Add `POWER_ON`, `BEEP_OFF`, and `RESCAN_GROVE` command handling stubs compatible with the new constants.**

### Task 4: ESP32 UART/COBS Hardening

**Files:**
- Modify: `main/app/esp32_rp2040.c`
- Modify: `main/app/indicator_sensor_model.c`

- [ ] **Step 1: Replace per-read frame decode with a persistent fragment buffer.**
- [ ] **Step 2: Append an explicit COBS delimiter when sending commands.**
- [ ] **Step 3: Send `POWER_ON` at startup instead of a startup beep.**
- [ ] **Step 4: Add dynamic packet validation/drop handling while preserving legacy parsing.**
- [ ] **Step 5: Fix sensor type argument validation from `&&` to `||`.**

### Task 5: Verification

**Files:**
- Modify: none

- [ ] **Step 1: Run `python3 scripts/verify_rp2040_protocol.py`.**
- [ ] **Step 2: Run `./build.sh` if ESP-IDF is available.**
- [ ] **Step 3: Run `cd rp2040 && pio run` if PlatformIO is available.**
- [ ] **Step 4: Report hardware checks still required: RP2040 upload, UART live sensor values, shutdown/power-on behavior.**
