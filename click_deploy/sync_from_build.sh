#!/usr/bin/env sh
set -eu

ROOT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEPLOY_DIR="$ROOT_DIR/click_deploy"

check_file() {
    src=$1
    label=$2
    if [ ! -f "$src" ]; then
        echo "Missing $label: $src" >&2
        return 1
    fi
}

copy_file() {
    src=$1
    dst=$2
    label=$3
    mkdir -p "$(dirname "$dst")"
    cp "$src" "$dst"
    echo "Copied $label -> $dst"
}

missing=0

ESP_BOOTLOADER="$ROOT_DIR/build/bootloader/bootloader.bin"
ESP_PARTITION="$ROOT_DIR/build/partition_table/partition-table.bin"
ESP_APP="$ROOT_DIR/build/indicator_ha.bin"
ESP_FLASHER_ARGS="$ROOT_DIR/build/flasher_args.json"
RP2040_UF2="$ROOT_DIR/rp2040/.pio/build/indicator_rp2040/firmware.uf2"
RP2040_ELF="$ROOT_DIR/rp2040/.pio/build/indicator_rp2040/firmware.elf"

check_file "$ESP_BOOTLOADER" "ESP32-S3 bootloader" || missing=1
check_file "$ESP_PARTITION" "ESP32-S3 partition table" || missing=1
check_file "$ESP_APP" "ESP32-S3 application" || missing=1
check_file "$ESP_FLASHER_ARGS" "ESP32-S3 flasher args" || missing=1
check_file "$RP2040_UF2" "RP2040 UF2 firmware" || missing=1
check_file "$RP2040_ELF" "RP2040 ELF firmware" || missing=1

if [ "$missing" -ne 0 ]; then
    cat >&2 <<'EOF'

One or more firmware artifacts are missing.

Build them from the repository root first:
  ./dev build
  ./dev rp2040 build

Then run:
  ./click_deploy/sync_from_build.sh
EOF
    exit 1
fi

copy_file "$ESP_BOOTLOADER" \
    "$DEPLOY_DIR/firmware/esp32s3/bootloader.bin" \
    "ESP32-S3 bootloader"

copy_file "$ESP_PARTITION" \
    "$DEPLOY_DIR/firmware/esp32s3/partition-table.bin" \
    "ESP32-S3 partition table"

copy_file "$ESP_APP" \
    "$DEPLOY_DIR/firmware/esp32s3/indicator_ha.bin" \
    "ESP32-S3 application"

copy_file "$ESP_FLASHER_ARGS" \
    "$DEPLOY_DIR/firmware/esp32s3/flasher_args.json" \
    "ESP32-S3 flasher args"

copy_file "$RP2040_UF2" \
    "$DEPLOY_DIR/firmware/rp2040/firmware.uf2" \
    "RP2040 UF2 firmware"

copy_file "$RP2040_ELF" \
    "$DEPLOY_DIR/firmware/rp2040/firmware.elf" \
    "RP2040 ELF firmware"

echo
echo "click_deploy firmware is up to date."
