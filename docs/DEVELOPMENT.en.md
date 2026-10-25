# Development

[简体中文](DEVELOPMENT.md) · [English README](../README.en.md)

## Project structure

- `src/main.cpp`: screen state machine, input, and scheduling.
- `src/model.cpp`, `src/battle_tick.bn_iwram.cpp`: administration, marching, and battle models.
- `src/render.bn_iwram.cpp`, `src/render_ui.cpp`: battlefield, map, and UI rendering.
- `src/music_decoder.cpp`, `src/audio.cpp`: ADPCM decoding and audio output.
- `src/save.cpp`: FLASH saves and historical format conversion.
- `include/`: interfaces, rules, constants, and header-only logic helpers.
- `generated/`: fixed build resources and data, checked into the repository.
- `tests/`: host model, rules, input, performance, and audio regression tests.
- `dependencies.lock.json`: pinned Butano commit, download URL, and SHA256.
- `dependencies/butano-patches.json`: checked save-marker patch applied after download.
- `vendor/butano/`: dependency installed automatically on first build; not committed and reused after verification.

## Validation

`bash tools/test.sh` compiles and runs the `*_tests.cpp` programs at the root of `tests/`, plus the integration skill-lifecycle fixture. Temporary executables are created in a temporary directory and removed on exit.

`bash build.sh` compiles the ROM and checks its header, FLASH size, and RAM usage. At least 8 KiB of IWRAM is reserved for the stack. `dist/build_report.json` records the results, commit ID when available, and toolchain lock-file hash.

Host tests do not establish correct GBA hardware behavior for display layers, audio timing, FLASH erase/write operations, or emulator compatibility. Validate changes in those areas using an emulator or hardware. The original project's automated gameplay infrastructure is not included, and a full automated campaign victory has not been reproduced here.

See the [English edition guide](ENGLISH.md) for localization generation and UI validation, and the [validation record](VALIDATION.en.md) for previously completed checks.

## Known limitations

- Rules differ from the original in places. Visual effects and performance under heavy load still need manual review.
- Some resources are available only as converted data; see the [asset guide](ASSETS.en.md).
- Asset rights are documented separately from the code license; see [asset provenance](ASSET_PROVENANCE.en.md).
- No cartridge hardware FLASH power-loss test has been provided.
- Hosted GitHub builds and releases require validation after pushing. Local tests cannot run those remote checks in advance.
