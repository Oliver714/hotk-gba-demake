# 参与开发

[English](CONTRIBUTING.en.md) | 简体中文

先阅读 README.md 和 docs/DEVELOPMENT.md。报告问题请附版本/提交、模拟器或实机信息、复现步骤和必要截图，不要上传原版游戏或未经许可的素材。

提交代码时保持修改范围集中，运行 `bash tools/test.sh` 和 `bash build.sh`。涉及渲染、音频或存档的变更，说明模拟器/实机验证情况。新素材必须附来源与许可；自有代码贡献使用 GPL-3.0-only，第三方代码保持其原许可。

generated/ 是源码仓库的一部分，更新转换输入时也应提交相应输出。不要提交 vendor/、.cache/、dist/、build/、个人存档、游玩历史或访问令牌。
