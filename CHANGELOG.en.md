# Changelog

[简体中文](CHANGELOG.md) · [English README](README.en.md)

## v0.2.1

- Added experimental English ROM and web editions with localized UI, separate game identifiers and saves, and build support for Linux, WSL, Docker, and Actions.
- Covered all 1,045 English source strings and revised 149 translations. Fixed overlapping, clipped, and misaligned text in menus, recruitment, officer profiles, maps, battle reports, and item messages. The existing ZLabs pixel font is retained.
- Added English versions of 12 Chinese documents, an English README, language switches, and links between the corresponding English guides.
- Actions now checks and publishes Chinese and English ROMs, ROM ZIPs, web ZIPs, and checksums. Release notes come from this version's Chinese and English changelog entries and are refreshed when a release is rerun.
- Added checks for translations, glyph coverage, word wrapping, compact ranks, and release-note extraction. Browser checks cover input, offline operation, save persistence, and mobile layout in both languages.

## v0.2.0

- Added a self-hostable web bundle running the same ROM through the mGBA browser core, with keyboard, touch controls, and browser saves.
- Actions now packages both ROM and HTML releases and checks sandboxed iframe operation, save persistence, and mobile layout.

## v0.1.0

- Established an independently buildable GBA project based on the original *Heroes of the Three Kingdoms 1*. GBA gameplay may differ from the original.
- Added asset conversion entry points, host regression tests, and a pinned toolchain installer.
- Added GitHub Actions builds and tag-based Release publishing.
- Licensed original project code under GPL-3.0-only, with separate records for third-party licenses and asset provenance that still requires verification.
