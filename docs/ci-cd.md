# GitHub CI/CD

The repository uses `.github/workflows/firmware.yml` as the single firmware
verification and release pipeline.

## Trigger matrix

| Trigger | Host checks | ESP32-S3 build | RP2040 build | Automatic tag | GitHub release |
| --- | --- | --- | --- | --- | --- |
| Pull request to `main` | Yes | Yes | Yes | No | No |
| Push to `main` | Yes | Yes | Yes | After all three jobs succeed | No; the new tag starts a separate run |
| Push tag matching `v*` | Yes | Yes | Yes | No | Yes |
| Manual dispatch | Yes | Yes | Yes | No | Only when dispatched from a `v*` tag |

New commits cancel older in-progress runs for the same branch. Tag builds are
never cancelled because they may be publishing immutable release evidence.
All jobs run on `ubuntu-24.04`; the workflow does not follow the moving
`ubuntu-latest` alias across runner-image migrations.

## Verification jobs

`Host checks` runs `python3 scripts/dev_check.py --skip-build`. This is the
fast gate for architecture rules, host behavior tests, protocol checks, UI
geometry, packaging constraints, and CI/CD regression tests.

`ESP32-S3 firmware` runs the repository's existing clean-by-default
`./dev build` command inside Espressif's official ESP-IDF container pinned to
v5.5.6. It uploads:

- bootloader and partition table binaries;
- `indicator_ha.bin` and `indicator_ha.elf`;
- `flasher_args.json` and `flash_args`;
- the generated `sdkconfig`;
- SHA-256 checksums.

`RP2040 firmware` installs PlatformIO 6.2.0 in an isolated virtual environment,
runs `./dev rp2040 build`, and uploads the UF2 and ELF with checksums. PlatformIO
packages are cached by `platformio.ini`. The Raspberry Pi platform repository is
pinned to a full commit SHA so a moving upstream branch cannot silently change
the firmware toolchain.

Artifacts from ordinary CI runs are retained for 14 days.

## Automatic SemVer tags

After a push to `main` passes Host checks, ESP32-S3 firmware, and RP2040
firmware, `auto-tag` runs `scripts/next_version.py` against the complete Git
history. It selects the numerically highest strict `vX.Y.Z` tag and ignores
other tag formats. With no valid tag, the baseline is `v0.0.0`.

Commits in `<latest>..HEAD` determine the increment, or all of `HEAD` when no
valid tag exists:

1. A `BREAKING CHANGE:` footer or a Conventional Commit `!` marker selects a
   major increment.
2. Otherwise, any `feat:` commit selects a minor increment.
3. Every other commit selects a patch increment.

No new commits is an error and does not create a tag. Before creating the
annotated tag as `github-actions[bot]`, the job checks origin again for the
candidate tag. An existing remote tag stops the job; the subsequent non-force
push also protects against a concurrent creator. Re-running therefore cannot
move an existing tag or create a duplicate.

The local tests exercise version calculation in temporary Git repositories and
check the workflow definition. The GitHub-hosted automatic tag path has not yet
been observed end to end; verify its first post-merge `main` run before treating
that automation as operationally proven.

## Firmware version source

At configure time, the root CMake file sets `PROJECT_VER` from:

```bash
git describe --tags --always --dirty --match 'v[0-9]*'
```

A clean checkout of an exact matching tag uses that tag as the app version.
Other checkouts retain the describe development suffix. If Git or repository
metadata is unavailable, the explicit fallback is `0.0.0-dev`, so source-archive
builds still work. The boot banner and Native `DeviceInfo.project_version` both
read the resulting ESP-IDF app descriptor; there is no separate firmware version
constant.

## Release process

A newly pushed automatic tag triggers this same workflow. Its tag build creates
fresh firmware artifacts, and the existing `release` job remains the only
GitHub Release publisher. It downloads the exact firmware produced by that tag
build and publishes:

- an ESP32-S3 archive;
- an RP2040 archive;
- a complete cross-platform click-deploy archive;
- `manifest.json`;
- `SHA256SUMS`.

To publish an existing `v*` tag manually, dispatch `firmware.yml` from that tag,
for example with `gh workflow run firmware.yml --ref vX.Y.Z`. Alternatively,
re-run the tag's existing workflow from GitHub Actions. Re-running updates
existing release assets with `--clobber` instead of creating duplicate releases.

## Main protection

The existing `main` branch protection remains unchanged. It requires strict,
up-to-date `Host checks`, `ESP32-S3 firmware`, and `RP2040 firmware` checks;
blocks force pushes and deletion; and requires review conversations to be
resolved. `enforce_admins=false` remains available for administrator emergency
handling. Automatic tagging happens after the protected push and does not
replace review or required checks.

## Security and maintenance

- Workflow permissions default to `contents: read`.
- Only `auto-tag` and the tag-only `release` job receive `contents: write`.
- Actions are pinned to immutable commit SHAs.
- Dependabot checks GitHub Actions updates monthly.
- The workflow never flashes attached hardware and does not read deployment
  secrets.
