# SenseCAP Indicator Click Deploy

This folder is a packaging scaffold for a firmware bundle that can flash both
chips in the SenseCAP Indicator:

- ESP32-S3 screen-side firmware
- RP2040 sensor coprocessor firmware

The repository does not commit generated firmware images or flashing tool
binaries. Build outputs are copied into this folder when preparing a release
package.

## Maintainer workflow

From the repository root:

```sh
./dev build
./dev rp2040 build
./click_deploy/sync_from_build.sh
```

Then zip the `click_deploy/` folder. A complete zip should contain:

```text
click_deploy/
  firmware/
    esp32s3/
      bootloader.bin
      partition-table.bin
      indicator_ha.bin
      flasher_args.json
    rp2040/
      firmware.elf
      firmware.uf2
  tools/
    esptool/
    picotool/
  macos_linux/
  windows/
```

The flashing scripts prefer bundled tools under `tools/`. If bundled tools are
not present, they fall back to `esptool`, `esptool.py`, and `picotool` on PATH.

## macOS / Linux

```sh
cd click_deploy
./macos_linux/install.sh
./macos_linux/flash_all.sh
```

To pin serial ports:

```sh
ESPPORT=/dev/cu.usbmodemXXXX ./macos_linux/flash_esp32s3.sh
RP2040_PORT=/dev/cu.usbmodemYYYY ./macos_linux/flash_rp2040.sh
```

## Windows PowerShell

```powershell
cd click_deploy
.\windows\flash_all.ps1
```

To pin serial ports:

```powershell
$env:ESPPORT = "COM7"
$env:RP2040_PORT = "COM8"
.\windows\flash_all.ps1
```
