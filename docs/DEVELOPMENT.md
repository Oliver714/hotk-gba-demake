# 开发结构

- src/main.cpp：页面状态、输入、调度。
- src/model.cpp、battle_tick.bn_iwram.cpp：内政、行军、战斗模型。
- src/render.bn_iwram.cpp、render_ui.cpp：战场、地图与界面绘制。
- src/music_decoder.cpp、audio.cpp：ADPCM 解码和音频输出。
- src/save.cpp：FLASH 存档及历史版本转换。
- include/：接口、容量、规则与纯逻辑辅助函数。
- generated/：参与编译的固定资源与数据；必须提交。
- tests/：主机端模型、布局、输入、技能及音频回归测试。
- dependencies.lock.json：固定 Butano 提交、下载地址和 SHA256。
- dependencies/butano-patches.json：下载后自动应用的存档标记补丁及校验值。
- vendor/butano/：首次构建自动生成的依赖目录，不提交；许可证随依赖下载保留。

## 验证

`bash tools/test.sh` 运行 tests/ 根目录的 *_tests.cpp 和技能生命周期夹具，输出及临时可执行文件位于临时目录，退出后自动清理。

`bash build.sh` 交叉编译并检查 ROM 头、FLASH 标记与 RAM 占用；IWRAM 保留至少 8 KiB 栈空间。发布信息保存在 dist/build_report.json，包含提交 ID（如果处于 Git 仓库中）及工具链锁文件哈希。

主机测试不能证明 GBA 实机上的图层、声音时序、FLASH 命令和完整通关流程正确。涉及这些部分的修改仍需模拟器或实机验证。这里未附带原工程的自动游玩基础设施，也没有声称已完成自动通关。

## 已知限制

- 与原作规则并非全面一致；视觉效果和高负载性能仍需持续检查。
- 一部分资源只有转换输出，见素材说明。
- 素材授权状态与代码协议分开记录。
- 尚未提供原子化实机 FLASH 断电测试。
- GitHub 托管构建需在推送后实际运行验证；本地测试不能代替远端执行。
