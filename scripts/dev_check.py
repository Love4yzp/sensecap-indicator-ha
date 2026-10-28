#!/usr/bin/env python3
"""Run local development checks for the ESP32S3 screen firmware."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def run(label: str, cmd: list[str]) -> None:
    print(f"\n==> {label}", flush=True)
    print("$ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=ROOT, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="run architecture checks only; useful when ESP-IDF is unavailable",
    )
    args = parser.parse_args()

    print("\n==> Host checks", flush=True)
    run("View event delivery", ["python3", "scripts/test_view_event_delivery.py"])
    run("Button press state", ["python3", "scripts/test_button_press_state.py"])
    run("Wi-Fi scan worker", ["python3", "scripts/test_wifi_scan_worker.py"])
    run("Wi-Fi scan UI state", ["python3", "scripts/test_wifi_scan_ui_state.py"])
    run("Architecture scan unit tests", ["python3", "scripts/test_architecture_scan.py"])
    run("Architecture scan", ["python3", "scripts/architecture_scan.py"])
    run("Project knowledge", ["python3", "scripts/test_project_knowledge.py"])
    run("RGB LCD config", ["python3", "scripts/test_lcd_rgb_config.py"])
    run("UI geometry", ["python3", "scripts/test_ui_geometry.py"])
    run("Click deploy package", ["python3", "scripts/test_click_deploy_package.py"])
    run("GitHub CI/CD", ["python3", "scripts/test_github_workflows.py"])
    run("Development CLI", ["python3", "scripts/test_dev_cli.py"])
    run("LVGL health diagnostics", ["python3", "scripts/test_lvgl_health.py"])
    run("HA switch synchronization", ["python3", "scripts/test_ha_switch_sync.py"])
    run("HA switch protocol", ["python3", "scripts/test_ha_switch_protocol.py"])
    run("RP2040 protocol constants", ["python3", "scripts/verify_rp2040_protocol.py"])

    if not args.skip_build:
        print("\n==> Firmware build", flush=True)
        run("ESP32-S3 clean build", ["./dev", "build"])
    else:
        print("\n==> Firmware build", flush=True)
        print("SKIPPED: --skip-build was requested", flush=True)

    print("\n==> Hardware smoke", flush=True)
    print(
        "NOT RUN: manual hardware verification is required; "
        "see docs/stability/hardware-smoke-checklist.md",
        flush=True,
    )

    print("\ndev_check: OK (hardware smoke pending)")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        print(f"\ndev_check: failed with exit code {exc.returncode}", file=sys.stderr)
        raise SystemExit(exc.returncode)
