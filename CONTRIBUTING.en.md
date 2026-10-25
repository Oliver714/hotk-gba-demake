# Contributing

[简体中文](CONTRIBUTING.md) · [English README](README.en.md)

Read the [README](README.en.md) and [development guide](docs/DEVELOPMENT.en.md) first. Bug reports should include the version or commit, emulator or hardware details, reproduction steps, and relevant screenshots. Do not upload the original game or assets without permission.

Keep changes focused. Run at least `bash tools/test.sh` and `bash build.sh`. For rendering, audio, or save changes, describe your emulator or hardware validation. Include provenance and licensing information for new assets. Contributions of original code use GPL-3.0-only; third-party dependencies retain their own licenses.

The `generated/` resources are part of the repository. When changing a converter, commit the corresponding generated results. Do not commit `vendor/`, `.cache/`, `dist/`, `build/`, personal saves, or large historical test-run directories.
