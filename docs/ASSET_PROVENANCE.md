# 素材来源与许可状态

此清单区分技术上能编译与已经确认再分发权限。GPL-3.0-only 仅为项目自有代码的协议，不能代替第三方素材授权。

| 内容 | 来源与状态 |
|---|---|
| assets/fonts | ZLabs Pixel、Adobe Source Han Mono；保留原字体和 OFL 许可。用于字形重建 |
| assets/ui/silk-title-v1.png | 历史记录为图像生成工具创建；具体提示见相邻 SOURCE.md |
| assets/battle 当前三张主图 | 历史生成图和后续编辑；步行/骑兵来源说明在相邻 SOURCE.md。主图的完整创作链尚需补齐 |
| assets/effects/battle-effects-v1.png | 历史生成效果图；相关来源记录随文件保留，完整权属仍需维护者核实 |
| generated/font_data.cpp、silk_ui_data.cpp | 字库及界面转换输出；字形源受 OFL 约束，其他内容依据各自来源 |
| generated/face_data.cpp | 人物头像提取自《三国群英传1》原版游戏；不声明为项目自有或 GPL 素材 |
| generated/terrain_map.cpp、menu_skin_data.cpp、部分 magic_*.cpp 等 | 历史导入或原作参考生成的资源表。未包含原版游戏资源包，但不据此声称这些输出获得自由再分发授权 |
| generated/game_data.cpp | 项目运行所需的数据表；含历史导入/参考内容，其具体来源与可再分发范围需逐项核实 |
| generated/music_*.adpcm、music_adpcm.cpp | 目前两首背景音乐提取自《三国志VI》的《逐鹿天下》和《烽火遍野》配乐（来源信息由维护者提供）；未取得自由再分发授权依据，不将其声明为 GPL 或公有领域。原 MP3 未收入仓库，可使用内置 tools/regenerate_assets.py music 脚本替换 |

发布前应补齐待核实项的授权依据，或替换为具有明确许可的独立素材。此项目未附带任何能够证明原版资源授权的文件。

仓库不包含原版游戏文件、SANGO.PAK、解包配置与工具。游戏以《三国群英传1》原版为 demake 参考，GBA 版玩法不一定与原版一致。
