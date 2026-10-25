# 本地验证记录（2026-10-23）

[English](VALIDATION.en.md) | 简体中文

- 固定工具链：24 个安装包，下载地址检查成功，SHA256 校验及临时目录解包通过。
- 从新仓库根目录编译，使用解包后的固定工具链成功链接；ROM 为 4 MiB。
- ROM SHA256：291b591ce8936955e98a41deb5ae6826b1f9e6edafd91bf1ce6a1b7cacb11153，与整理前当前版本一致。
- EWRAM 252372 bytes；IWRAM 24272 bytes，保留至少 8 KiB 栈空间。
- 默认集合 62 个主机测试程序已完整执行通过，技能生命周期夹具通过。长期模型检查覆盖 28 个可选战役、666 次战斗；不等于自动通关证明。
- 在临时副本执行 battle、effects、ui、font 四组素材生成成功，重建字库与 UI 的 C++ 语法检查通过。正式目录的资源缓存保持当前发布版内容。
- Release 打包流程通过；Actions 工作流通过 actionlint v1.7.12 检查。
- 远端构建与发布验证结果以仓库 Actions 和 Releases 页面为准。

本记录是整理时的快照，不代表之后所有提交的验证结果。

2026-10-24 网页版验证：Windows 和 Linux 容器均可打包约 5 MiB 的 HTML 网页包；包内 ROM 哈希不变。真实浏览器在无跨域隔离的沙盒 iframe 中验证启动、键盘导航、128 KiB 存档、刷新恢复和触屏布局，通过时无外部资源请求及运行错误。Actions 已加入同类 Chromium 检查，远端结果以对应提交的运行记录为准。

Butano 自动下载验证：移走原有 vendor 后，按 dependencies.lock.json 的固定归档及 SHA256 重新安装，存档标记补丁的前后校验通过；重复调用复用已有依赖。Linux 和不携带宿主机 vendor 的 Docker 构建均成功，ROM SHA256 不变；Release ZIP 内下载所得的 Butano 许可证已检查。

Docker 验证：固定 Ubuntu 22.04 amd64 基础镜像，从联网下载工具链到编译、校验和本地导出完整执行成功，ROM SHA256 与上文一致。Dockerfile 通过 Buildx 配置检查；tools/build_docker.sh 的自定义输出目录已实际验证。此项验证使用默认构建模式，未在容器内重复运行此前已通过的 62 项主机测试。

项目说明、内部标识符与注释统一后再次构建通过，ROM 哈希不变；模型、远程攻击节奏及技能生命周期检查再次通过。

