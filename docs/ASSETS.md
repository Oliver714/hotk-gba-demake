# 素材维护

[English](ASSETS.en.md) | 简体中文

正常构建只使用已提交的 generated/，不会自动重建素材。这样不会因字体环境或时间戳改变而意外修改 ROM。

## 可独立执行的转换

先安装依赖：`python3 -m pip install -r requirements-assets.txt`。锁定 Pillow 9.0.1 对应当前验证环境 Python 3.10；可在虚拟环境安装，不影响纯编译。

```bash
python3 tools/regenerate_assets.py battle
python3 tools/regenerate_assets.py effects
python3 tools/regenerate_assets.py ui
python3 tools/regenerate_assets.py font
```

- battle：修改 assets/battle 的 v14 主图、v4 步行图、v5 骑兵图，再生成基础、压缩、缩放精灵和混色表。
- effects：修改独立效果图，再生成效果表、行跨度缓存和混色表。历史 magic_* 表不会因此被重新生成。
- ui：修改主题 JSON、标题背景，重建 UI 图像、专用字形和调色板。
- font：保留现有字形集合并扫描当前代码、数据中的新字符；使用 ZLabs 12px，缺字由 SourceHanMono 补齐。字形数量、排版及 ROM 校验值可能改变。

每次只重建需要的组，检查 generated/ 的差异，运行测试并编译，再在模拟器中检查视觉效果。修改共享调色板时，仍需检查历史缓存资源的颜色一致性。

## 替换音乐

目前两首背景音乐提取自《三国志VI》的《逐鹿天下》和《烽火遍野》配乐。本仓库保留压缩后的音乐数据，未附带原 MP3。可通过内置脚本替换为其他音乐：准备自己有权使用的 `audio_design/主游戏.mp3` 与 `audio_design/战斗.mp3`，安装系统 ffmpeg，然后运行：

```bash
python3 tools/regenerate_assets.py music
```

这会覆盖已提交的 ADPCM 与元数据，并生成可忽略的试听 WAV。记录新音乐作者与许可后，提交生成结果。音频压缩工具不会自动证明音乐的授权。

人物头像提取自《三国群英传1》原版游戏。主字体 ZLabs Pixel 12px 与补字字体 Source Han Mono 均来自第三方，原字体、来源说明与 OFL 许可证位于 assets/fonts/。

## 只保留转换结果的素材

地图、头像、部分特效与游戏数据没有捆绑原版输入和历史导入器。可以直接维护对应生成表，或编写独立替代生成器；本包不提供从原版文件重新导入这些内容的入口。
