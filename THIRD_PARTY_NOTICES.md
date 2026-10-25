# Third-party notices

The project's original code is licensed under GPL-3.0-only. This does not relicense independently licensed dependencies or grant rights to third-party art, audio, names or other content.

| Component | License / source |
|---|---|
| Butano | zlib; fetched from https://github.com/GValiente/butano at the revision in dependencies.lock.json. Downloaded license: vendor/butano/LICENSE |
| Browser runtime | EmulatorJS 4.2.3 (GPL-3.0), with its mGBA/RetroArch core; upstream sources and hashes in web-runtime.lock.json. mGBA is MPL-2.0; the core archives carry their own license.txt and core.json. See the [web guide](docs/WEB.en.md) for the checked frontend patches and source links |
| Bundled Butano components | Fetched with Butano. Individual notices in vendor/butano/licenses and source headers; preserve these files |
| ZLabs Pixel 12px M CN | SIL OFL 1.1; assets/fonts/ZLabsPixel-OFL.txt and ZLabsPixel-SOURCE.md |
| Source Han Mono SC Regular | SIL OFL 1.1; assets/fonts/SourceHanMono-LICENSE.md and SourceHanMono-SOURCE.md |
| Background music | Extracted from Romance of the Three Kingdoms VI (《三国志VI》): 《逐鹿天下》 and 《烽火遍野》, as identified by the maintainer. Not relicensed under GPL; replace using tools/regenerate_assets.py music |
| Character portraits | Extracted from the original 《三国群英传1》 game; not project-owned GPL assets |
| Art, data, other audio and compiled resource tables | Mixed provenance; see [asset provenance](docs/ASSET_PROVENANCE.en.md). Not covered by a blanket GPL declaration |
| Wonderful Toolchain | Downloaded development dependency, not vendored in this repository; licenses accompany upstream packages. The build also downloads target-gba-nonfree for the standard boot-header/logo bytes used by wf-gbatool; this is not project-owned GPL code |

Font binaries are included unmodified for optional regeneration. Their OFL notices also accompany release archives. Only rasterized glyphs are linked into the ROM.

Release archives include this notice, the project's GPL text, font notices and Butano notices. When distributing a modified ROM, provide the corresponding source and build instructions as required by the applicable licenses.

The project applies a checked, minimal save-detection patch to Butano after download (dependencies/butano-patches.json). It changes the default SRAM marker to FLASH1M for this cartridge. Upstream notices remain intact.
