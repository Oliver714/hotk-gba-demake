# GitHub Actions and releases

[简体中文](RELEASING.md) · [English README](../README.en.md)

Workflow: `.github/workflows/build.yml`.

1. Pushes to `main`/`master`, pull requests, and manual runs install the pinned toolchain, run host regression tests, download pinned Butano as needed, build and validate ROMs, and upload Actions artifacts.
2. Pushing a `v*` tag runs the same build and validation jobs. A separate release job creates or updates the matching GitHub Release and uploads ROMs, ROM ZIPs, web ZIPs, and `SHA256SUMS.txt`.
3. Only the release job has `contents: write`; build jobs use read-only permissions. The workflow uses `GITHUB_TOKEN`, so no personal access token is needed. Actions are pinned by commit SHA.

For example:

```bash
git tag v0.1.0
git push origin v0.1.0
```

Enable Actions in the repository settings. Organization policies must also permit the release job to write repository contents. A normal manual run produces build artifacts without publishing a Release; maintainers should create release tags.

Toolchain package URLs, versions, and SHA256 hashes are pinned. The installer does not silently select the latest version. If an upstream repository removes an old package, update the lock file through an explicit version change and run regression tests rather than disabling verification. Downloads and GitHub runner images depend on external services, so indefinite byte-for-byte reproducibility is not guaranteed.

Inspect local release contents with `python3 tools/package_release.py` after building the ROM. ZIPs include the project license, third-party notices, and asset licensing status. ELF files and caches are excluded. Corresponding source is available from the repository at the same tag.

Before release, update `CHANGELOG.md` and the [English changelog](../CHANGELOG.en.md), verify [asset licensing status](ASSET_PROVENANCE.en.md), and complete manual emulator checks. Each changelog must contain a level-two heading matching the tag exactly, such as `## v0.2.0`. The workflow extracts that version's entries into Chinese and English sections in the Release body. Rerunning the same tag also replaces the existing Release notes. If either entry is missing, empty, or duplicated, the tagged build fails before toolchain installation rather than falling back to generated notes.

Preview the notes and run their tests locally:

```bash
python3 tools/release_notes.py --tag v0.2.0 --output .cache/release-notes.md
python3 tests/release_notes_tests.py
```

GitHub's generated source archives contain committed sources and build scripts at the tag.

Butano itself is not committed. Actions installs it through `build.sh`, and release ZIPs retain the license notices for the version used. The workflow builds and packages both languages; see the [English edition guide](ENGLISH.md) and [web packaging guide](WEB.en.md) for artifact names and language options.
