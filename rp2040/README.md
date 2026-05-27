# RP2040 Firmware

This directory is a PlatformIO project for the SenseCAP Indicator RP2040 coprocessor.
The RP2040 firmware currently uses PlatformIO for compile, upload, dependency,
and serial monitor management.

## Build

Install PlatformIO, then build from this directory:

```sh
cd rp2040
pio run
```

## Upload

Connect the Indicator RP2040 USB port and run:

```sh
cd rp2040
pio run -t upload
```

The project targets `seeed_indicator_rp2040` with the Earle Philhower Arduino
RP2040 core.

## Serial Monitor

```sh
cd rp2040
pio device monitor
```

The monitor speed is `115200`.

## Dependencies

PlatformIO installs the RP2040 sensor dependencies declared in `platformio.ini`:

- `PacketSerial`
- `AHT20`
- `Sensirion I2C SCD4x`
- `Sensirion I2C SGP40`
- `Sensirion Gas Index Algorithm`
