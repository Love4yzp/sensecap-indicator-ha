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

    run("Architecture scan", ["python3", "scripts/architecture_scan.py"])
    run("RGB LCD config", ["python3", "scripts/test_lcd_rgb_config.py"])
    run("UI geometry", ["python3", "scripts/test_ui_geometry.py"])
    run("Click deploy package", ["python3", "scripts/test_click_deploy_package.py"])

    if not args.skip_build:
        run("Firmware build", [sys.executable, "scripts/dev.py", "build"])

    print("\ndev_check: OK")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as exc:
        print(f"\ndev_check: failed with exit code {exc.returncode}", file=sys.stderr)
        raise SystemExit(exc.returncode)
