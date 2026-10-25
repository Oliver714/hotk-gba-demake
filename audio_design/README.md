# 背景音乐替换

[English](README.en.md) | 简体中文

目前两首背景音乐提取自《三国志VI》的《逐鹿天下》和《烽火遍野》配乐。仓库保留转换后的音乐数据，不附带原 MP3。

可用内置脚本替换为其他音乐。将自己有权使用的音乐放在：

- 主游戏音乐：audio_design/主游戏.mp3
- 战斗音乐：audio_design/战斗.mp3

安装 ffmpeg 后，在仓库根目录执行：

```bash
python3 tools/regenerate_assets.py music
bash build.sh
```

脚本会重建 generated/ 中的压缩音乐数据。音乐不属于项目代码的 GPL 授权范围，详见 [素材来源清单](../docs/ASSET_PROVENANCE.md)。
