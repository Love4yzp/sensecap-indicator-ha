#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

chmod +x "$ROOT_DIR"/macos_linux/*.sh
find "$ROOT_DIR/tools" -type f \( -name esptool -o -name picotool \) -exec chmod +x {} \; 2>/dev/null || true

echo "click_deploy scripts are ready."
echo "Run: ./macos_linux/flash_all.sh"
