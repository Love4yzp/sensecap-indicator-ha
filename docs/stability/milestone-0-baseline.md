# Milestone 0: ESP-IDF v5.5.6 stability baseline

Recorded on 2026-10-27 on branch `idf55-stability-baseline`, based on firmware
commit `02fb020`.

This baseline changes dependency and verification metadata only. It does not
change BSP, LCD/touch buses, DMA/timing, the RP2040 wire protocol, partitions,
eFuse configuration, Home Assistant topics/payloads, or runtime behavior.

## Fixed build inputs

| Input | Version / identity |
| --- | --- |
| ESP-IDF | tag `v5.5.6`, commit `880a203ae603d08a94d06aa01e0d6761958e5e9e` |
| Python | `3.14.7` |
| CMake | `4.4.3` |
| Ninja | `1.13.2` |
| Xtensa GCC | `esp-14.2.0_20260121`, GCC `14.2.0` |
| esptool | `4.12.0` |
| LVGL | `9.6.0~1`, component hash `7d82410747bbfb319531449749e940f440f8414f041f9eaba13f0d1dd7f58cdd` |
| esp_lvgl_port | `2.9.0`, component hash `d3c020b45c3dfc0d1706a83b63a1c34a6eb444a6e935f9afff79f92b7b94dcef` |
| Target | `esp32s3` |

`main/idf_component.yml` fixes the three version constraints.
`dependencies.lock` records the resolved registry components, hashes, target,
and IDF version. `managed_components/`, `sdkconfig`, and `build/` remain
generated and ignored.

## Reproducing the build

Use an exact ESP-IDF tag, not `release/v5.5`:

```bash
git clone --branch v5.5.6 --recursive \
  https://github.com/espressif/esp-idf.git /path/to/esp-idf-v5.5.6
/path/to/esp-idf-v5.5.6/install.sh esp32s3

export IDF_PATH=/path/to/esp-idf-v5.5.6
python3 scripts/dev_check.py --skip-build
./dev build
```

The repository launcher sources `$IDF_PATH/export.sh` only for ESP32-S3
commands when `idf.py` is not already active. It does not hard-code or modify a
global SDK location. `./dev build` remains clean-by-default.

## Commands actually run

The SDK was prepared as a detached temporary worktree from the fixed tag, with
shallow submodules. Missing IDF tools were installed through the tag's own
`idf_tools.py` into the user's existing `~/.espressif` tool directory; no
system package manager or shell profile was changed.

```bash
git -C /home/spencer/esp/esp-idf-v5.5.4 fetch --depth=1 origin tag v5.5.6
git -C /home/spencer/esp/esp-idf-v5.5.4 worktree add \
  --detach /tmp/esp-idf-v5.5.6 v5.5.6
git -c url."https://github.com/".insteadOf=git@github.com: \
  submodule update --init --recursive --depth 1

python3 scripts/dev_check.py --skip-build
IDF_PATH=/tmp/esp-idf-v5.5.6 ./dev build
IDF_PATH=/tmp/esp-idf-v5.5.6 ./dev build
```

The first host-check run exposed an existing scanner false positive: an LVGL
call shown inside a C comment was treated as executable code. The scanner now
masks comments and literals while preserving source offsets, and has a unit
test for that behavior.

The first full build resolved LVGL `9.6.0~1` and esp_lvgl_port `2.9.0`. Only
after that successful build were the exact manifest constraints and lock file
added. Subsequent clean builds with those pins succeeded with the same size
metrics. The hashes below are from the final verified build; application and
bootloader hashes can change when embedded project/version metadata changes.

## Build result

| Artifact / memory class | Result |
| --- | --- |
| `indicator_ha.bin` | `0x1b2640` / 1,779,264 bytes |
| App partition | `0x700000` / 7,340,032 bytes |
| App partition free | `0x54d9c0` / 5,560,768 bytes (76%) |
| `bootloader.bin` | `0x5590` / 21,904 bytes |
| Bootloader free | `0x2a70` / 10,864 bytes (33%) |
| Total ELF image size | 1,779,147 bytes |
| Flash code | 1,144,264 bytes |
| Flash data | 494,516 bytes |
| DIRAM | 146,239 / 341,760 bytes (42.79%) |
| IRAM | 16,384 / 16,384 bytes (100%; configured layout) |

Artifact SHA-256 values from the final verified clean build:

```text
indicator_ha.bin              7aac6cb89f1d584ee81e8c0aacee5374415a544928d7d725f21a90c6a2010411
bootloader/bootloader.bin     9414af51bbc733033a0434cb2221f8717ab70347d974a198a95a9525fd82c968
partition-table.bin           a7dfdccd649702098427a73dbc716db3ff9e51753271738f1d9d1f9ba0a5c67d
```

## Known build warnings

The successful v5.5.6 configure emits these existing warnings:

- `LV_COLOR_SCREEN_TRANSP` is an unknown Kconfig symbol in
  `sdkconfig.defaults`.
- `LV_SPRINTF_USE_FLOAT` is an unknown Kconfig symbol in
  `sdkconfig.defaults`.
- ESP-IDF reports `Missing kconfig option. Re-run the build process` while
  regenerating `sdkconfig`.
- Component Manager emits an empty CMake warning at `build.cmake:631`.

The root `CMakeLists.txt` currently uses `add_compile_options(... -w)`, so C/C++
compiler warnings are suppressed by existing project behavior. Milestone 0
records this limitation; it does not change warning policy or Kconfig behavior.

## Verification boundary

Host checks and full firmware builds are automated through
`scripts/dev_check.py`. Hardware smoke remains manual and must be recorded
against `hardware-smoke-checklist.md`.

Not run during this software-only baseline:

- flashing or serial boot-log capture;
- LCD, touch, backlight, sleep/wake, or factory-reset hardware tests;
- RP2040 UART traffic or soak testing;
- real Wi-Fi, MQTT broker, or Home Assistant interoperability;
- ESP-IDF 6.x compile or hardware canary.

Milestone 1 must not start until the hardware checklist has either been
executed or its remaining risk is explicitly accepted.
