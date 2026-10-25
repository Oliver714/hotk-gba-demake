# English edition

[English README](../README.en.md)

The English edition is built as a separate ROM. Chinese remains the default language. The build scripts generate English sources and font resources in `.cache/localization/en/`, then compile them with the same toolchain. There is no runtime translation lookup.

For general setup and deployment, see the [build guide](BUILD.en.md) and [web guide](WEB.en.md).

## Building

Linux / WSL:

```sh
GAME_LANGUAGE=en bash build.sh
GAME_LANGUAGE=en bash tools/test.sh
python3 tools/package_release.py --language en
```

Windows:

```powershell
.\build.ps1 -Language en
```

The ROM is written to `dist/sango_gba_en.gba`; the English web bundle is `release/hotk-gba-demake-web-en.zip`. After building both ROMs, run `python3 tools/package_release.py --language all` to package both languages and generate a shared checksum manifest. The Actions workflow also builds, tests, and packages both languages.

For Docker, add `--build-arg GAME_LANGUAGE=en` to the existing build command. Both the `artifacts` and `web-artifacts` targets support this argument.

## Translation and typography

The translation catalog is `locales/en.json`. Officer and city names use Pinyin without tone marks. Narrow columns use readable short names; the battle status bar uses compact skill names. Rank abbreviations are explicitly defined in `short_roles` rather than produced by cutting off words. The font uses the ASCII subset of the existing ZLabs pixel font. Flags use two compact letters, with distinct abbreviations for rulers whose names would otherwise collide.

All 1,045 source strings were reviewed, and 149 entries were revised. City administrators are consistently called `Governor`; `Finish` ends the administration phase. Battle results distinguish `Retreat`, `Captive`, and `Defeat`.

| Previous wording | Current wording |
|---|---|
| One affair per officer each year | One action per officer per year |
| Move attacks faster | Faster attacks while moving |
| Front Fwd / Front Back | Advance Front / Pull Back |
| Gen Charge | Send General |
| Equipment equipped | Item equipped |

`python3 tools/localization.py` checks translation coverage and format placeholders. `python3 tools/prepare_locale.py` generates the English source tree, fonts, layout patches, and manifest. Generation fails if a translation is missing or a layout patch no longer matches its source. This edition is experimental; translations and layouts may receive further revisions.

## Separate save files

Save compatibility between the Chinese and English ROMs is not required. The English ROM has its own filename and game code, `SGE1`; keep separate save files in your emulator. The English web edition uses the game name `hotk-gba-demake-en`, ID `1002`, and ROM path `game-en.gba` to store saves separately from the Chinese edition. These identifiers should remain stable across future English releases.

Save testing covers saving, loading, and restoring progress after a browser refresh within the English edition. It does not cover transferring saves between languages.

## UI adaptations

`tools/prepare_locale.py` and `tools/english_layout.py` apply layout changes to the generated English tree. Shared Chinese sources and resources remain unchanged.

| Area | Adaptation |
|---|---|
| Menus and messages | Administration buttons are uniformly widened and left-aligned. Confirmation and result messages wrap at word boundaries; item effects can span two lines. |
| Rosters and profiles | Names, status labels, and numbers have separate fields. Narrow columns use compact names, ranks, troop types, and formations. Names are positioned clear of portraits. |
| Map and armies | Ownership labels distinguish player cities from other factions. Departure, destination, and route fields are separated; army lists display `Retreat` in full. |
| Battle | HUD, setup cards, and troop allocation fields have revised spacing. Battle reports and duel results separate names, outcomes, and destinations. |
| Saves and ending | Save summaries use short chapter and ruler names. The victory epilogue shows the full ruler name and `has united China.` on separate lines. |
| Characters and controls | Supported ASCII characters replace missing arrows. Control hints are shortened while preserving their meaning. |

## Validation

- The 13 localization tests cover placeholders, glyph coverage, compact rank widths, and word-wrapping boundaries in the generated message renderer. Translation coverage is 1,045/1,045 source strings.
- mGBA layout review covered all 56 screen types and 98 fixed states, including long names, three-digit HP/MP, final roster pages, messages, and confirmation dialogs. These fixtures check layout rather than a complete campaign. Separate input sequences exercised the production ROM's main menu, return confirmation, recruitment, and officer switching.
- Both ROMs passed build, size, header checksum, and memory checks. The Chinese ROM checksum remained unchanged during English adaptation. The full host test suites previously passed for both languages; localization tests were rerun after the final wording revisions.
- Both web bundles passed checks for keyboard input, offline operation, 128 KiB save persistence, and mobile layout.

Screenshots and audit ROMs are local cache artifacts and are excluded from Git and release packages. Rerun the relevant checks after changing text or layouts; this record does not validate subsequent changes automatically.
