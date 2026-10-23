#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname -- "${BASH_SOURCE[0]}")/.."
if [[ $# -ne 0 ]]; then echo 'Usage: bash tools/build.sh' >&2; exit 2; fi
python3 tools/bootstrap_deps.py
if [[ -z "${DEVKITARM:-}" && -z "${WONDERFUL_TOOLCHAIN:-}" ]]; then
    export WONDERFUL_TOOLCHAIN=/opt/wonderful
fi
make -j"${BUILD_JOBS:-4}"
python3 tools/check_rom.py --output sango_gba.gba
