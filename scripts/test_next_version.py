#!/usr/bin/env python3
"""Integration tests for next_version.py using real temporary Git repositories."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "scripts" / "next_version.py"


class NextVersionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.repo = Path(self.temporary.name)
        self.git("init", "-q", "-b", "main")
        self.git("config", "user.name", "Version Test")
        self.git("config", "user.email", "version-test@example.invalid")
        self.counter = 0

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def git(self, *args: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["git", *args],
            cwd=self.repo,
            text=True,
            capture_output=True,
            check=True,
        )

    def commit(self, message: str) -> None:
        self.counter += 1
        path = self.repo / "history.txt"
        with path.open("a", encoding="utf-8") as output:
            output.write(f"{self.counter}: {message}\n")
        self.git("add", "history.txt")
        self.git("commit", "-q", "-m", message)

    def tag(self, version: str) -> None:
        self.git("tag", "-a", version, "-m", version)

    def calculate(self) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["python3", str(SCRIPT)],
            cwd=self.repo,
            text=True,
            capture_output=True,
        )

    def assert_version(self, expected: str) -> None:
        result = self.calculate()
        self.assertEqual(0, result.returncode, result.stderr)
        self.assertEqual(expected + "\n", result.stdout)
        self.assertEqual("", result.stderr)

    def test_fix_is_patch(self) -> None:
        self.commit("chore: initial")
        self.tag("v1.2.3")
        self.commit("fix: correct state")
        self.assert_version("v1.2.4")

    def test_feature_is_minor_even_with_later_fix(self) -> None:
        self.commit("chore: initial")
        self.tag("v1.2.3")
        self.commit("feat: add control")
        self.commit("fix: correct control")
        self.assert_version("v1.3.0")

    def test_breaking_footer_is_major(self) -> None:
        self.commit("chore: initial")
        self.tag("v1.2.3")
        self.commit("fix: replace API\n\nBREAKING CHANGE: clients must migrate")
        self.assert_version("v2.0.0")

    def test_conventional_bang_is_major(self) -> None:
        self.commit("chore: initial")
        self.tag("v1.2.3")
        self.commit("feat(api)!: replace API")
        self.assert_version("v2.0.0")

    def test_highest_numeric_tag_wins_across_gaps(self) -> None:
        self.commit("chore: initial")
        self.tag("v1.2.0")
        self.tag("v1.9.0")
        self.commit("docs: intermediate release")
        self.tag("v1.10.0")
        self.commit("fix: correct state")
        self.assert_version("v1.10.1")

    def test_no_tag_uses_zero_baseline(self) -> None:
        self.commit("feat: first feature")
        self.assert_version("v0.1.0")

    def test_no_tag_patch_starts_at_zero_zero_one(self) -> None:
        self.commit("chore: initial")
        self.assert_version("v0.0.1")

    def test_no_new_commits_is_an_error(self) -> None:
        self.commit("feat: first feature")
        self.tag("v1.0.0")
        result = self.calculate()
        self.assertNotEqual(0, result.returncode)
        self.assertEqual("", result.stdout)
        self.assertIn("No new commits since v1.0.0", result.stderr)

    def test_non_semver_tags_are_ignored(self) -> None:
        self.commit("chore: initial")
        self.tag("v1.2.3")
        self.commit("fix: correct state")
        self.tag("v99.0")
        self.tag("v99.0.0-beta.1")
        self.tag("v01.0.0")
        self.tag("release-99.0.0")
        self.assert_version("v1.2.4")

    def test_other_commit_formats_are_patch(self) -> None:
        self.commit("chore: initial")
        self.tag("v1.2.3")
        self.commit("Update documentation")
        self.assert_version("v1.2.4")


if __name__ == "__main__":
    unittest.main(verbosity=2)
