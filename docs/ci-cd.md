# GitHub CI/CD

The repository uses `.github/workflows/firmware.yml` as the single firmware
verification and release pipeline.

## Trigger matrix

| Trigger | Host checks | ESP32-S3 build | RP2040 build | GitHub release |
| --- | --- | --- | --- | --- |
| Pull request to `main` | Yes | Yes | Yes | No |
| Push to `main` | Yes | Yes | Yes | No |
| Push tag matching `v*` | Yes | Yes | Yes | Yes |
| Manual dispatch | Yes | Yes | Yes | Only when dispatched from a `v*` tag |

New commits cancel older in-progress runs for the same branch. Tag builds are
never cancelled because they may be publishing immutable release evidence.

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

## Release process

Create and push an annotated version tag after `main` is green:

```bash
git tag -a vX.Y.Z -m "vX.Y.Z"
git push origin vX.Y.Z
```

The release job downloads the exact firmware produced by the tag build and
publishes:

- an ESP32-S3 archive;
- an RP2040 archive;
- a complete cross-platform click-deploy archive;
- `manifest.json`;
- `SHA256SUMS`.

Re-running a tag workflow updates existing release assets with `--clobber`
instead of creating duplicate releases.

## Security and maintenance

- Workflow permissions default to `contents: read`.
- Only the tag-only release job receives `contents: write`.
- Actions are pinned to immutable commit SHAs.
- Dependabot checks GitHub Actions updates monthly.
- The workflow never flashes attached hardware and does not read deployment
  secrets.
