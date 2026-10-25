# Replacing background music

[简体中文](README.md) · [English README](../README.en.md)

The two current tracks are extracted from *Romance of the Three Kingdoms VI*: *Zhu Lu Tian Xia* (逐鹿天下) and *Feng Huo Bian Ye* (烽火遍野). The repository contains converted music data, not the original MP3 files.

To replace them, place music you have permission to use at these exact paths:

- Main game: `audio_design/主游戏.mp3`
- Battle: `audio_design/战斗.mp3`

Install ffmpeg, then run from the repository root:

```bash
python3 tools/regenerate_assets.py music
bash build.sh
```

The script regenerates the compressed music data in `generated/`. The project's GPL license does not cover rights to the music; see the [asset provenance inventory](../docs/ASSET_PROVENANCE.en.md).
