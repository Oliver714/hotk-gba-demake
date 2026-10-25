#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname -- "${BASH_SOURCE[0]}")/.."
if [[ $# -ne 0 ]]; then echo 'Usage: bash tools/build.sh' >&2; exit 2; fi
python3 tools/bootstrap_deps.py
if [[ -z "${DEVKITARM:-}" && -z "${WONDERFUL_TOOLCHAIN:-}" ]]; then
    export WONDERFUL_TOOLCHAIN=/opt/wonderful
fi
case "${GAME_LANGUAGE:-zh}" in
    zh)
        make -j"${BUILD_JOBS:-4}"
        python3 tools/check_rom.py --output sango_gba.gba
        ;;
    en)
        python3 tools/prepare_locale.py
        make -C .cache/localization/en GAME_LANGUAGE=en -j"${BUILD_JOBS:-4}"
        python3 tools/check_rom.py --language en --input .cache/localization/en/sango_gba_en.gba --elf .cache/localization/en/sango_gba_en.elf --output sango_gba_en.gba
        ;;
    *) echo 'GAME_LANGUAGE must be zh or en' >&2; exit 2 ;;
esac
