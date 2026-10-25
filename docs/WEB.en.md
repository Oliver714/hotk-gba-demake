# Web packaging and deployment

[简体中文](WEB.md) · [English README](../README.en.md)

The web edition runs the same GBA ROM through EmulatorJS and its mGBA WebAssembly core. It does not reimplement game logic. A standalone ROM download is still provided.

## Package locally

Build the ROM using the [build guide](BUILD.en.md). On Ubuntu/Linux, install `p7zip-full` with `sudo apt-get install p7zip-full`. Windows' bundled bsdtar can read the runtime archive directly.

```bash
python3 tools/package_release.py
```

This produces:

- `release/sango_gba.gba`: standalone ROM.
- `release/sango-gba.zip`: ROM, licenses, and source notices.
- `release/hotk-gba-demake-web.zip`: self-hostable web bundle.
- `release/SHA256SUMS.txt`: checksums for release artifacts.

To build only the web bundle, run `python3 tools/build_web.py`. The first run downloads the pinned EmulatorJS 4.2.3 distribution, approximately 290 MiB, verifies the SHA256 in `web-runtime.lock.json`, and caches it in `.cache/web/`. The output includes only the frontend assets and single-threaded mGBA core; the web ZIP is roughly 5 MiB.

All runtime paths are local to the bundle. No external CDN, account, backend, or GBA BIOS is required. The single-threaded core does not require SharedArrayBuffer or COOP/COEP headers. A modern desktop browser is recommended; touch controls are available on mobile, with performance depending on the browser and device.

### English bundle

After building the English ROM, run:

```bash
python3 tools/package_release.py --language en
```

The output is `release/hotk-gba-demake-web-en.zip`, with a preview directory at `dist/web-en/`. Packaging copies `web/index.en.html` to the bundle's root as `index.html`, omits the extra `index.en.html`, and loads `game-en.gba`. The source templates remain unchanged. Use `--language all` after building both ROMs to package both languages.

English saves have separate identifiers from Chinese saves; see the [English edition guide](ENGLISH.md#separate-save-files).

## Local preview

```bash
python3 -m http.server 8000 --directory dist/web
```

Open http://localhost:8000/ rather than opening `index.html` through `file://`. Click the start button to enable audio. For English, serve `dist/web-en` instead.

Default keys: arrow keys for the D-pad, X for A, Z for B, Enter for Start, Shift for Select, A for L, and S for R. The emulator menu supports remapping, fullscreen, and save import/export.

Save through the in-game menu first. The web emulator syncs FLASH data to browser storage every five seconds. Wait briefly before leaving; export a backup as well. Saves belong to the current site's browser storage. Clearing site data, private browsing, switching browsers, or changing the hosting origin can make earlier saves unavailable. Emulator save states and in-game saves are separate formats.

## Docker

```text
docker buildx build --platform linux/amd64 --target web-artifacts --output type=local,dest=./release .
```

This target builds the ROM, downloads and trims the runtime, and exports the web ZIP. The existing `artifacts` target and `tools/build_docker.sh` still export ROM build results only. Add `--build-arg GAME_LANGUAGE=en` for an English bundle.

## Deploy

1. Upload `hotk-gba-demake-web.zip` to a host supporting HTML games, or extract it into a static site directory. Use the `-en` ZIP for English.
2. Use the ZIP root's `index.html` as the entry point. Preserve directory structure and filename case.
3. For iframe embedding, a recommended starting size is 800×680. Allow scripts, same-origin storage, and fullscreen. A full-page layout is also supported.
4. SharedArrayBuffer support is not needed. Test sound, touch controls, and save recovery before publishing.
5. Offer `sango-gba.zip` separately for ROM downloads (`sango-gba-en.zip` for English).

All emulator assets are bundled and loaded through relative paths. The HTTP server must serve JavaScript, JSON, and binary files correctly.

## Actions and third-party runtime

Normal builds produce web artifacts and run browser checks. Pushing a `v*` tag uploads ROMs, web bundles, and checksums to the same Release.

EmulatorJS uses GPL-3.0; mGBA uses MPL-2.0. The actual core includes RetroArch and related dependencies. Their archives are pinned in the lock file and retain `license.txt` and `core.json`. The web bundle includes a `licenses/` directory and source notices. The frontend has two checked changes: an explicit local version check and IndexedDB save synchronization. Packaging verifies the modification points; the upstream hashes and source references remain applicable to the unmodified core archives. See [third-party notices](../THIRD_PARTY_NOTICES.md).

For local browser smoke tests, install Playwright 1.64.1 and Chromium, then run `node tests/web_smoke.cjs`. You can set `NODE_PATH` to a temporary dependency directory; Node.js is not required to play the game. Checks cover a sandboxed iframe without cross-origin isolation, no external resource requests, input navigation, 128 KiB FLASH saves, refresh recovery, and touch layout. For English, use `GAME_LANGUAGE=en node tests/web_smoke.cjs dist/web-en`.
