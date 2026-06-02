#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCRIPT_PATH="${BASH_SOURCE[0]}"
ELF="$ROOT_DIR/firmware/rp2040/firmware.elf"
PORT="${1:-${RP2040_PORT:-}}"

if [ ! -f "$ELF" ]; then
  echo "Missing RP2040 firmware: $ELF" >&2
  exit 1
fi

platform_dir() {
  case "$(uname -s):$(uname -m)" in
    Darwin:arm64)  echo "macos-arm64" ;;
    Darwin:x86_64) echo "macos-amd64" ;;
    Linux:x86_64)  echo "linux-amd64" ;;
    Linux:aarch64|Linux:arm64) echo "linux-aarch64" ;;
    *) echo "" ;;
  esac
}

PICOTOOL=""
PLATFORM_DIR="$(platform_dir)"
if [ -n "$PLATFORM_DIR" ] && [ -x "$ROOT_DIR/tools/picotool/$PLATFORM_DIR/picotool" ]; then
  PICOTOOL="$ROOT_DIR/tools/picotool/$PLATFORM_DIR/picotool"
elif command -v picotool >/dev/null 2>&1; then
  PICOTOOL="$(command -v picotool)"
else
  echo "No bundled picotool and no picotool on PATH." >&2
  exit 1
fi

picotool_device_count() {
  ("$PICOTOOL" info -d 2>&1 || true) \
    | awk 'BEGIN { count=0 } /^ *type:/ { count++ } END { print count }'
}

find_rp2040_port_macos() {
  ioreg -p IOService -l -w 0 2>/dev/null \
    | awk '
      /"USB Product Name" = "INDICATOR RP2040"/ { found=1 }
      /"kUSBProductString" = "INDICATOR RP2040"/ { found=1 }
      /"IOCalloutDevice" =/ {
        if (found) {
          dev=$0
          sub(/^.*"IOCalloutDevice" = "/, "", dev)
          sub(/".*$/, "", dev)
          print dev
          exit
        }
      }
    '
}

find_rp2040_port_linux() {
  for tty in /sys/class/tty/ttyACM* /sys/class/tty/ttyUSB*; do
    [ -e "$tty" ] || continue
    dev_path=$(readlink -f "$tty/device" 2>/dev/null || true)
    while [ -n "$dev_path" ] && [ "$dev_path" != "/" ]; do
      if [ -f "$dev_path/idVendor" ] && [ -f "$dev_path/idProduct" ]; then
        vid=$(cat "$dev_path/idVendor")
        pid=$(cat "$dev_path/idProduct")
        case "$vid:$pid" in
          2886:0050|2e8a:00c0) echo "/dev/$(basename "$tty")"; return 0 ;;
        esac
      fi
      dev_path=$(dirname "$dev_path")
    done
  done
}

touch_1200bps() {
  port="$1"
  case "$(uname -s)" in
    Darwin) stty -f "$port" 1200 cs8 -cstopb -parenb || true ;;
    Linux)  stty -F "$port" 1200 cs8 -cstopb -parenb || true ;;
  esac
}

if [ -z "$PORT" ]; then
  case "$(uname -s)" in
    Darwin) PORT="$(find_rp2040_port_macos || true)" ;;
    Linux)  PORT="$(find_rp2040_port_linux || true)" ;;
  esac
fi

before_count="$(picotool_device_count)"

if [ "$before_count" -gt 0 ]; then
  echo "RP2040 BOOTSEL device is already visible to picotool."
else
  if [ -z "$PORT" ]; then
    echo "RP2040 serial port was not auto-detected and no BOOTSEL device is visible." >&2
    echo "Set RP2040_PORT and retry, for example:" >&2
    echo "  RP2040_PORT=/dev/cu.usbmodemXXXX $SCRIPT_PATH" >&2
    exit 1
  fi

  echo "Triggering RP2040 BOOTSEL through $PORT"
  touch_1200bps "$PORT"

  echo "Waiting for RP2040 BOOTSEL device..."
  for _ in $(seq 1 30); do
    now_count="$(picotool_device_count)"
    if [ "$now_count" -gt "$before_count" ] || [ "$now_count" -gt 0 ]; then
      break
    fi
    sleep 0.2
  done

  if [ "$(picotool_device_count)" -eq 0 ]; then
    echo "RP2040 BOOTSEL device was not found by picotool." >&2
    exit 1
  fi
fi

echo "Flashing RP2040 with picotool"
"$PICOTOOL" load -v -x "$ELF"
