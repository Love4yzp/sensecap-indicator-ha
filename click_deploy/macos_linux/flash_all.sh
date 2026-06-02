#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"$ROOT_DIR/macos_linux/flash_rp2040.sh"
"$ROOT_DIR/macos_linux/flash_esp32s3.sh"

echo "Both firmware images have been flashed."
