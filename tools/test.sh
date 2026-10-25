#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname -- "${BASH_SOURCE[0]}")/.."
case "${GAME_LANGUAGE:-zh}" in
    zh) ;;
    en) python3 tools/prepare_locale.py; cd .cache/localization/en ;;
    *) echo 'GAME_LANGUAGE must be zh or en' >&2; exit 2 ;;
esac
test_dir="$(mktemp -d)"
trap 'rm -rf -- "$test_dir"' EXIT
if (( $# )); then
    test_sources=("$@")
else
    test_sources=(tests/*_tests.cpp)
fi
for source in "${test_sources[@]}"; do
    suite="$(basename "$source" .cpp)"
    case "$suite" in
      effect_blend_tests) sources=(src/effect_blend.cpp generated/additive_palette.cpp) ;;
      music_decoder_tests) sources=(src/music_decoder.cpp generated/music_adpcm.cpp) ;;
      save_slot_ui_tests) sources=() ;;
      *) sources=(src/model.cpp src/battle_tick.bn_iwram.cpp generated/game_data.cpp generated/terrain_map.cpp) ;;
    esac
    "${CXX:-g++}" -std=c++17 -O2 -Iinclude "${sources[@]}" "$source" -o "$test_dir/$suite"
    if [[ "$suite" == music_decoder_tests ]]; then
        "$test_dir/$suite" generated/music_world.adpcm "$test_dir/world.pcm"
        "$test_dir/$suite" generated/music_battle.adpcm "$test_dir/battle.pcm"
    else
        "$test_dir/$suite"
    fi
done
"${CXX:-g++}" -std=c++17 -O2 -Iinclude src/model.cpp src/battle_tick.bn_iwram.cpp generated/game_data.cpp generated/terrain_map.cpp tests/skill_review_fixture.cpp -o "$test_dir/lifecycle"
"$test_dir/lifecycle" > "$test_dir/lifecycle.tsv"
echo 'PASS: all host suites and skill lifecycle fixture'
