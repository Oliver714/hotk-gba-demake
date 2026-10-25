# Local validation record (2026-10-23)

[简体中文](VALIDATION.md) · [English README](../README.en.md)

- All 24 package URLs in the pinned toolchain setup downloaded successfully. SHA256 verification and installation into a temporary directory passed.
- The project built and linked from the new repository root using the downloaded pinned toolchain. The ROM is 4 MiB.
- ROM SHA256: `291b591ce8936955e98a41deb5ae6826b1f9e6edafd91bf1ce6a1b7cacb11153`, identical to the current version before the directory was split out.
- EWRAM usage: 252372 bytes. IWRAM usage: 24272 bytes, leaving at least 8 KiB for the stack.
- The default set of 62 host test programs compiled and passed. The integration skill-lifecycle fixture passed. A fast model check covered 28 selectable campaigns and 666 battle rounds; this is not evidence of an automated campaign victory.
- In a temporary copy, the `battle`, `effects`, `ui`, and `font` asset generators completed successfully. Regenerated UI C++ passed syntax checking. The working project retained its existing generated resource versions.
- Release packaging passed. The Actions workflow passed actionlint v1.7.12.
- Remote build and release results must be checked on the repository's Actions and Releases pages.

This is a snapshot of checks performed at the time, not validation of later changes.

## Web bundle validation (2026-10-24)

Windows and Linux packaging both produced an HTML web ZIP of roughly 5 MiB without changing the bundled ROM hash. A real browser in a sandboxed iframe without cross-origin isolation passed startup, keyboard navigation, 128 KiB save persistence, refresh recovery, and touch-layout checks. No external resource requests or runtime errors were observed in the passing run. Actions includes the equivalent Chromium checks; consult the run for the corresponding commit for remote results.

## Automatic Butano setup

After removing the previous `vendor/`, Butano was reinstalled from the archive pinned in `dependencies.lock.json` with SHA256 verification. Pre- and post-patch checks for the save marker passed. Repeated setup reused the cache and did not modify sources. Native Linux and Docker builds without host-vendored dependencies passed with the same ROM SHA256. Release ZIPs were checked for the license notices of the Butano version used.

## Docker validation

Using the pinned Ubuntu 22.04 amd64 base image, dependency downloads, toolchain installation, compilation, validation, and local artifact export all passed. The ROM SHA256 matched the native build. The Dockerfile passed Buildx configuration checks, and `tools/build_docker.sh` was tested with a custom output directory. This run used the default build mode; it did not rerun the previously passing 62 host tests inside the container.

## Naming cleanup

After project descriptions, internal identifiers, and comments were standardized, another build passed with the same ROM hash. Model, ranged-friendly-blocking, and integration skill-lifecycle checks passed again.

Later English localization and layout checks are recorded in the [English edition guide](ENGLISH.md#validation).
