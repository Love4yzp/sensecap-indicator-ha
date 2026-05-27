#!/usr/bin/env python3
"""Verify ESP32 and RP2040 protocol constants stay in sync."""

from __future__ import annotations

import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
ESP32_HEADER = ROOT / "main/app/esp32_rp2040.h"
RP2040_HEADER = ROOT / "rp2040/include/indicator_rp2040.hpp"

REQUIRED_VALUES = {
    "PKT_TYPE_NONE": 0x00,
    "PKT_TYPE_CMD_COLLECT_INTERVAL": 0xA0,
    "PKT_TYPE_CMD_BEEP_ON": 0xA1,
    "PKT_TYPE_CMD_BEEP_OFF": 0xA2,
    "PKT_TYPE_CMD_SHUTDOWN": 0xA3,
    "PKT_TYPE_CMD_POWER_ON": 0xA4,
    "PKT_TYPE_CMD_RESCAN_GROVE": 0xA5,
    "PKT_TYPE_SENSOR_SCD41_CO2": 0xB2,
    "PKT_TYPE_SENSOR_SHT41_TEMP": 0xB3,
    "PKT_TYPE_SENSOR_SHT41_HUMIDITY": 0xB4,
    "PKT_TYPE_SENSOR_SGP40_TVOC_INDEX": 0xB5,
    "PKT_TYPE_SENSOR_ATTACHED": 0xB8,
    "PKT_TYPE_SENSOR_DETACHED": 0xB9,
    "PKT_TYPE_SENSOR_VALUE": 0xBA,
    "PKT_SENSOR_ID_AHT20_TEMP": 0,
    "PKT_SENSOR_ID_AHT20_HUMIDITY": 1,
    "PKT_SENSOR_ID_SCD41_CO2": 2,
    "PKT_SENSOR_ID_SGP40_VOC": 3,
    "PKT_SENSOR_ID_SCD41_TEMP": 4,
    "PKT_SENSOR_ID_SCD41_HUMIDITY": 5,
    "PKT_SENSOR_ID_GROVE_BASE": 0x10,
    "PKT_SENSOR_CAT_TEMP": 0,
    "PKT_SENSOR_CAT_HUMIDITY": 1,
    "PKT_SENSOR_CAT_CO2": 2,
    "PKT_SENSOR_CAT_VOC": 3,
    "PKT_SENSOR_CAT_LIGHT": 4,
    "PKT_SENSOR_CAT_PRESSURE": 5,
    "PKT_SENSOR_CAT_PM": 6,
    "PKT_SENSOR_CAT_NOX": 7,
    "PKT_SENSOR_CAT_UNKNOWN": 255,
}


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//.*", "", text)
    return text


def parse_int(value: str) -> int:
    return int(value.strip(), 0)


def parse_constants(path: Path) -> dict[str, int]:
    text = strip_comments(path.read_text())
    constants: dict[str, int] = {}

    for match in re.finditer(r"#define\s+(PKT_[A-Z0-9_]+)\s+([xXa-fA-F0-9]+)", text):
        constants[match.group(1)] = parse_int(match.group(2))

    for enum_match in re.finditer(r"enum\s+[A-Za-z0-9_]*\s*\{(.*?)\};", text, flags=re.S):
        current = -1
        for entry in enum_match.group(1).split(","):
            entry = entry.strip()
            if not entry:
                continue
            assign = re.match(r"(PKT_[A-Z0-9_]+)\s*(?:=\s*([xXa-fA-F0-9]+))?$", entry)
            if assign is None:
                continue
            if assign.group(2) is not None:
                current = parse_int(assign.group(2))
            else:
                current += 1
            constants[assign.group(1)] = current

    return constants


def main() -> int:
    esp32 = parse_constants(ESP32_HEADER)
    rp2040 = parse_constants(RP2040_HEADER)

    errors: list[str] = []
    for name, expected in REQUIRED_VALUES.items():
        for label, constants in (("ESP32", esp32), ("RP2040", rp2040)):
            actual = constants.get(name)
            if actual is None:
                errors.append(f"{label} missing {name}")
            elif actual != expected:
                errors.append(f"{label} {name}=0x{actual:02X}, expected 0x{expected:02X}")

        if name in esp32 and name in rp2040 and esp32[name] != rp2040[name]:
            errors.append(f"mismatch {name}: ESP32=0x{esp32[name]:02X}, RP2040=0x{rp2040[name]:02X}")

    if errors:
        print("Protocol verification failed:")
        for error in errors:
            print(f"  - {error}")
        return 1

    print(f"Protocol verification passed for {len(REQUIRED_VALUES)} constants.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
