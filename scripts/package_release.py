#!/usr/bin/env python3
"""Create deterministic firmware release archives from CI build artifacts."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import tempfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
FIXED_ZIP_TIMESTAMP = (1980, 1, 1, 0, 0, 0)

ESP32_FILES = (
    "bootloader.bin",
    "partition-table.bin",
    "indicator_ha.bin",
    "indicator_ha.elf",
    "flasher_args.json",
    "flash_args",
    "sdkconfig",
    "SHA256SUMS",
)
RP2040_FILES = (
    "firmware.uf2",
    "firmware.elf",
    "SHA256SUMS",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def require_files(directory: Path, names: tuple[str, ...]) -> None:
    missing = [name for name in names if not (directory / name).is_file()]
    if missing:
        joined = ", ".join(missing)
        raise SystemExit(f"Missing release input in {directory}: {joined}")


def write_zip(source: Path, destination: Path, prefix: str) -> None:
    with zipfile.ZipFile(
        destination,
        mode="w",
        compression=zipfile.ZIP_DEFLATED,
        compresslevel=9,
    ) as archive:
        for path in sorted(candidate for candidate in source.rglob("*") if candidate.is_file()):
            relative = path.relative_to(source)
            info = zipfile.ZipInfo(str(Path(prefix) / relative), FIXED_ZIP_TIMESTAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (path.stat().st_mode & 0xFFFF) << 16
            archive.writestr(info, path.read_bytes())


def copy_named_files(source: Path, destination: Path, names: tuple[str, ...]) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    for name in names:
        shutil.copy2(source / name, destination / name)


def write_checksums(directory: Path) -> None:
    files = sorted(
        path for path in directory.rglob("*")
        if path.is_file() and path.name != "SHA256SUMS"
    )
    lines = [
        f"{sha256(path)}  {path.relative_to(directory)}"
        for path in files
    ]
    (directory / "SHA256SUMS").write_text(
        "\n".join(lines) + "\n",
        encoding="utf-8",
    )


def package_release(
    version: str,
    commit: str,
    esp32_dir: Path,
    rp2040_dir: Path,
    output_dir: Path,
) -> None:
    require_files(esp32_dir, ESP32_FILES)
    require_files(rp2040_dir, RP2040_FILES)
    output_dir.mkdir(parents=True, exist_ok=True)

    safe_version = version.replace("/", "-")
    project_prefix = f"sensecap-indicator-ha-{safe_version}"
    esp32_archive = output_dir / f"{project_prefix}-esp32s3.zip"
    rp2040_archive = output_dir / f"{project_prefix}-rp2040.zip"
    click_archive = output_dir / f"{project_prefix}-click-deploy.zip"

    write_zip(esp32_dir, esp32_archive, f"{project_prefix}-esp32s3")
    write_zip(rp2040_dir, rp2040_archive, f"{project_prefix}-rp2040")

    with tempfile.TemporaryDirectory() as temporary:
        stage = Path(temporary) / "click_deploy"
        shutil.copytree(ROOT / "click_deploy", stage)
        copy_named_files(
            esp32_dir,
            stage / "firmware" / "esp32s3",
            (
                "bootloader.bin",
                "partition-table.bin",
                "indicator_ha.bin",
                "indicator_ha.elf",
                "flasher_args.json",
                "flash_args",
                "sdkconfig",
            ),
        )
        copy_named_files(
            rp2040_dir,
            stage / "firmware" / "rp2040",
            ("firmware.uf2", "firmware.elf"),
        )
        write_checksums(stage)
        write_zip(stage, click_archive, f"{project_prefix}-click-deploy")

    archives = (esp32_archive, rp2040_archive, click_archive)
    manifest = {
        "version": version,
        "commit": commit,
        "artifacts": [
            {
                "name": archive.name,
                "bytes": archive.stat().st_size,
                "sha256": sha256(archive),
            }
            for archive in archives
        ],
    }
    (output_dir / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    (output_dir / "SHA256SUMS").write_text(
        "\n".join(f"{sha256(archive)}  {archive.name}" for archive in archives) + "\n",
        encoding="utf-8",
    )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", required=True)
    parser.add_argument("--commit", default=os.environ.get("GITHUB_SHA", "unknown"))
    parser.add_argument("--esp32-dir", type=Path, required=True)
    parser.add_argument("--rp2040-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    package_release(
        args.version,
        args.commit,
        args.esp32_dir.resolve(),
        args.rp2040_dir.resolve(),
        args.output_dir.resolve(),
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
