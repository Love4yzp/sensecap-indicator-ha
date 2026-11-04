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
        self.assertEqual(5, workflow.count("runs-on: ubuntu-24.04"))
        self.assertIn("esp_idf_version: v5.5.6", workflow)
        self.assertIn("target: esp32s3", workflow)
        self.assertIn("command: ./dev build", workflow)
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
        self.assertEqual(2, workflow.count("contents: write"))
        self.assertEqual(1, workflow.count("gh release create"))
        self.assertEqual(1, workflow.count("gh release upload"))

        uses = re.findall(r"^\s*uses:\s*([^\s#]+)", workflow, re.MULTILINE)
        self.assertGreaterEqual(len(uses), 11)
        for action in uses:
            self.assertRegex(action, r"^[^@]+@[0-9a-f]{40}$")

    def test_auto_tag_job_is_main_only_and_preserves_release_boundary(self) -> None:
        workflow = WORKFLOW.read_text(encoding="utf-8")
        jobs = workflow.split("\njobs:\n", 1)[1]
        job_names = re.findall(r"^  ([a-z0-9-]+):$", jobs, re.MULTILINE)
        self.assertEqual(["host", "esp32", "rp2040", "auto-tag", "release"], job_names)

        auto_tag = workflow.split("\n  auto-tag:\n", 1)[1].split("\n  release:\n", 1)[0]
        self.assertIn(
            "if: github.event_name == 'push' && github.ref == 'refs/heads/main'",
            auto_tag,
        )
        self.assertIn("needs: [host, esp32, rp2040]", auto_tag)
        self.assertIn("permissions:\n      contents: write", auto_tag)
        self.assertIn("fetch-depth: 0", auto_tag)
        self.assertIn("python3 scripts/next_version.py", auto_tag)
        self.assertIn('git ls-remote --tags origin "refs/tags/${VERSION}"', auto_tag)
        self.assertIn('git config user.name "github-actions[bot]"', auto_tag)
        self.assertIn('git tag -a "${VERSION}" -m "${VERSION}" "${GITHUB_SHA}"', auto_tag)
        self.assertIn(
            'git push origin "refs/tags/${VERSION}:refs/tags/${VERSION}"',
            auto_tag,
        )
        self.assertNotIn("--force", auto_tag)
        self.assertNotIn("gh release", auto_tag)

    def test_firmware_version_comes_from_git_and_app_descriptor(self) -> None:
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        main = (ROOT / "main" / "main.c").read_text(encoding="utf-8")
        native = (ROOT / "main" / "ha" / "native_api_server.c").read_text(
            encoding="utf-8",
        )

        self.assertIn(
            'COMMAND git describe --tags --always --dirty --match "v[0-9]*"',
            cmake,
        )
        self.assertIn('set(PROJECT_VER "${GIT_DESCRIBE_VERSION}")', cmake)
        self.assertIn('set(PROJECT_VER "0.0.0-dev")', cmake)
        self.assertLess(cmake.index("execute_process("), cmake.index("project.cmake"))
        self.assertLess(cmake.index("project.cmake"), cmake.index("project(indicator_ha)"))
        self.assertIn('#include "esp_app_desc.h"', main)
        self.assertIn("esp_app_get_description()->version", main)
        self.assertNotIn("#define VERSION", main)
        self.assertNotIn("v1.1.0", main)
        self.assertIn("esp_app_get_description()", native)
        self.assertIn("app->version", native)

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
