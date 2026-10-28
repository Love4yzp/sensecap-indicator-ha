#!/usr/bin/env python3
"""Regression checks for GitHub Actions CI/CD and release packaging."""

from __future__ import annotations

import hashlib
import json
import re
import subprocess
import tempfile
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
WORKFLOW = ROOT / ".github" / "workflows" / "firmware.yml"
DEPENDABOT = ROOT / ".github" / "dependabot.yml"


def write_inputs(directory: Path, names: tuple[str, ...]) -> None:
    directory.mkdir(parents=True)
    for name in names:
        (directory / name).write_bytes(f"test:{name}\n".encode())


class GithubWorkflowTests(unittest.TestCase):
    def test_workflow_uses_least_privilege_and_pinned_actions(self) -> None:
        workflow = WORKFLOW.read_text(encoding="utf-8")

        self.assertNotIn("pull_request_target", workflow)
        self.assertIn("permissions:\n  contents: read", workflow)
        self.assertIn("cancel-in-progress:", workflow)
        self.assertNotIn("ubuntu-latest", workflow)
        self.assertEqual(4, workflow.count("runs-on: ubuntu-24.04"))
        self.assertIn("esp_idf_version: v5.5.6", workflow)
        self.assertIn("target: esp32s3", workflow)
        self.assertIn("platformio==6.2.0", workflow)
        platformio = (ROOT / "rp2040" / "platformio.ini").read_text(
            encoding="utf-8",
        )
        self.assertIn(
            "platform-raspberrypi.git#"
            "5d4561a05e3b212660ac6fdd3fbfb328d1988aa1",
            platformio,
        )
        self.assertIn("startsWith(github.ref, 'refs/tags/v')", workflow)
        self.assertIn("contents: write", workflow)
        self.assertIn("gh release create", workflow)
        self.assertIn("gh release upload", workflow)

        uses = re.findall(r"^\s*uses:\s*([^\s#]+)", workflow, re.MULTILINE)
        self.assertGreaterEqual(len(uses), 8)
        for action in uses:
            self.assertRegex(action, r"^[^@]+@[0-9a-f]{40}$")

    def test_dependabot_updates_github_actions(self) -> None:
        text = DEPENDABOT.read_text(encoding="utf-8")

        self.assertIn('package-ecosystem: "github-actions"', text)
        self.assertIn('directory: "/"', text)
        self.assertIn('interval: "monthly"', text)

    def test_release_packager_builds_target_and_click_deploy_archives(self) -> None:
        esp32_names = (
            "bootloader.bin",
            "partition-table.bin",
            "indicator_ha.bin",
            "indicator_ha.elf",
            "flasher_args.json",
            "flash_args",
            "sdkconfig",
            "SHA256SUMS",
        )
        rp2040_names = ("firmware.uf2", "firmware.elf", "SHA256SUMS")

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            esp32 = root / "esp32s3"
            rp2040 = root / "rp2040"
            output = root / "release"
            write_inputs(esp32, esp32_names)
            write_inputs(rp2040, rp2040_names)

            subprocess.run(
                [
                    "python3",
                    str(ROOT / "scripts" / "package_release.py"),
                    "--version",
                    "v9.9.9",
                    "--commit",
                    "0123456789abcdef",
                    "--esp32-dir",
                    str(esp32),
                    "--rp2040-dir",
                    str(rp2040),
                    "--output-dir",
                    str(output),
                ],
                cwd=ROOT,
                check=True,
            )

            expected = {
                "sensecap-indicator-ha-v9.9.9-esp32s3.zip",
                "sensecap-indicator-ha-v9.9.9-rp2040.zip",
                "sensecap-indicator-ha-v9.9.9-click-deploy.zip",
                "manifest.json",
                "SHA256SUMS",
            }
            self.assertEqual(expected, {path.name for path in output.iterdir()})

            click_archive = output / "sensecap-indicator-ha-v9.9.9-click-deploy.zip"
            with zipfile.ZipFile(click_archive) as archive:
                names = set(archive.namelist())
            prefix = "sensecap-indicator-ha-v9.9.9-click-deploy"
            self.assertIn(
                f"{prefix}/firmware/esp32s3/indicator_ha.bin",
                names,
            )
            self.assertIn(
                f"{prefix}/firmware/rp2040/firmware.uf2",
                names,
            )
            self.assertIn(f"{prefix}/SHA256SUMS", names)

            manifest = json.loads(
                (output / "manifest.json").read_text(encoding="utf-8"),
            )
            self.assertEqual("v9.9.9", manifest["version"])
            self.assertEqual("0123456789abcdef", manifest["commit"])
            self.assertEqual(3, len(manifest["artifacts"]))
            for artifact in manifest["artifacts"]:
                path = output / artifact["name"]
                digest = hashlib.sha256(path.read_bytes()).hexdigest()
                self.assertEqual(digest, artifact["sha256"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
