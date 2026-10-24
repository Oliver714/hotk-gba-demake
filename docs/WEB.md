# 网页版与 itch.io

网页版使用 EmulatorJS 的 mGBA WebAssembly 核心运行同一个 GBA ROM，并非另写一套游戏逻辑。ROM 下载版继续发布。

## 本地打包

先按 BUILD.md 构建 ROM。Ubuntu/Linux 另需 `sudo apt-get install p7zip-full`；Windows 自带 bsdtar 可直接读取运行时归档。

```bash
python3 tools/package_release.py
```

这会同时生成：

- release/sango_gba.gba：原始 ROM。
- release/sango-gba.zip：ROM、构建报告及许可说明。
- release/hotk-gba-demake-web.zip：上传 itch.io 的网页包。
- release/SHA256SUMS.txt：以上三个附件的校验值。

也可只打网页包：`python3 tools/build_web.py`。第一次下载固定的 EmulatorJS 4.2.3 发布归档（约 290 MiB），依照 web-runtime.lock.json 校验 SHA256 后缓存到 .cache/web/；最终只保留网页资源和 mGBA 单线程核心，网页 ZIP 约 5 MiB。

游戏运行时全部使用包内相对路径，不需要外部 CDN、账号或服务端，不附带 GBA BIOS。单线程核心无需 SharedArrayBuffer 或 COOP/COEP 响应头。现代桌面浏览器优先；手机可使用触屏按键，性能与兼容性因设备而异。

## 本地预览

```bash
python3 -m http.server 8000 --directory dist/web
```

打开 http://localhost:8000/。不要双击 index.html 用 file:// 打开。点击“开始游戏”启用声音。

默认键位：方向键移动，X=A，Z=B，Enter=Start，Shift=Select，A=L，S=R。模拟器菜单可以修改映射、切换全屏和导出/导入存档。

请先在游戏内保存；网页模拟器每 5 秒将 FLASH 存档同步至浏览器数据库。建议保存后稍候数秒，并定期导出备份。存档按当前网站和浏览器环境保存，清理网站数据、无痕模式、切换浏览器或更换托管域名可能导致旧存档不可用。模拟器即时存档不能替代跨版本的游戏内存档。

## Docker

```text
docker buildx build --platform linux/amd64 --target web-artifacts --output type=local,dest=./release .
```

该目标会构建 ROM 并导出全部发布附件，包括网页 ZIP。原来的 artifacts 目标和 tools/build_docker.sh 仍用于仅导出 ROM。

## itch.io 设置

1. Kind of project 选择 HTML / HTML Game。
2. 上传 hotk-gba-demake-web.zip，勾选 This file will be played in the browser。
3. 另行上传 sango-gba.zip 供需要 ROM 的玩家下载，不要把它标记为浏览器运行文件。
4. 页面嵌入可先设 800×680，并启用全屏；也可选 Click to launch in fullscreen。
5. 不需要启用 SharedArrayBuffer 支持。先用 Draft 检查声音、键位和存档后再公开。

ZIP 根目录包含 index.html；所有游戏资源都在同一个 ZIP 内。参考官方要求：https://itch.io/docs/creators/html5

## Actions 与依赖许可

普通构建生成所有附件并运行网页启动检查；推送 v* 标签自动将 ROM、网页包和校验值上传同一个 Release。

EmulatorJS 使用 GPL-3.0，mGBA 使用 MPL-2.0，核心还包含 RetroArch 等组件。完整上游归档在锁文件中固定，核心内部附带 license.txt、core.json，网页包的 licenses/ 包含来源与许可说明。项目对前端应用了明确的本地版本检查及 IndexedDB 存档同步修改，打包脚本对修改上下文进行检查，并保留相应可读源文件。第三方美术、字体和音乐的来源说明仍然适用。

浏览器集成检查：安装 Playwright 1.64.1 和 Chromium，然后运行 `node tests/web_smoke.cjs`；通过 NODE_PATH 指向测试依赖目录即可，无需在游戏运行时安装 Node.js。检查覆盖无跨域隔离的沙盒 iframe、无外部资源请求、按键操作、128 KiB FLASH 存档、刷新恢复及触屏布局。
