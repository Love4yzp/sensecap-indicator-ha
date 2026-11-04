#!/usr/bin/env python3
"""Calculate the next vX.Y.Z tag from local Git history."""

from __future__ import annotations

import re
import subprocess
import sys


SEMVER_TAG = re.compile(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")
CONVENTIONAL_SUBJECT = re.compile(
    r"^(?P<type>[A-Za-z][A-Za-z0-9-]*)"
    r"(?:\([^()\r\n]+\))?(?P<breaking>!)?:[ \t]+\S",
)
BREAKING_FOOTER = re.compile(r"^BREAKING CHANGE:", re.MULTILINE)


class VersionError(Exception):
    pass


def git(*args: str) -> str:
    result = subprocess.run(
        ["git", *args],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        detail = result.stderr.strip() or "unknown git error"
        raise VersionError(f"git {args[0]} failed: {detail}")
    return result.stdout.strip()


def change_level(message: str) -> int:
    if BREAKING_FOOTER.search(message):
        return 3
    match = CONVENTIONAL_SUBJECT.match(message)
    if match and match.group("breaking"):
        return 3
    if match and match.group("type") == "feat":
        return 2
    return 1


def next_version() -> str:
    tags = git("tag", "--list").splitlines()
    versions: list[tuple[tuple[int, int, int], str]] = []
    for tag in tags:
        match = SEMVER_TAG.fullmatch(tag)
        if match:
            version = tuple(int(part) for part in match.groups())
            versions.append((version, tag))

    latest = max(versions) if versions else None
    revision_range = f"{latest[1]}..HEAD" if latest else "HEAD"
    commits = git("rev-list", revision_range).splitlines()
    if not commits:
        if latest:
            raise VersionError(f"No new commits since {latest[1]}")
        raise VersionError("No commits on HEAD")

    level = max(change_level(git("show", "-s", "--format=%B", commit)) for commit in commits)
    major, minor, patch = latest[0] if latest else (0, 0, 0)
    if level == 3:
        candidate = (major + 1, 0, 0)
    elif level == 2:
        candidate = (major, minor + 1, 0)
    else:
        candidate = (major, minor, patch + 1)

    version = "v{}.{}.{}".format(*candidate)
    if version in tags:
        raise VersionError(f"Tag {version} already exists")
    return version


def main() -> int:
    try:
        print(next_version())
    except VersionError as error:
        print(f"next_version: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
