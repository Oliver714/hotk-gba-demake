# Linux、WSL 与 Docker 编译

[English](BUILD.en.md) | 简体中文

所有命令在仓库根目录运行。正常编译直接使用 generated/，不需要原版游戏文件、素材原稿、字体转换工具或 ffmpeg。

## Linux 原生编译

当前固定工具链针对 **Linux x86_64**。以 Ubuntu 22.04 / Debian 系发行版为例：

```bash
sudo apt-get update
sudo apt-get install -y ca-certificates python3 make g++ xz-utils
sudo mkdir -p /opt/wonderful
sudo chown "$(id -u):$(id -g)" /opt/wonderful
python3 tools/install_toolchain.py
bash build.sh
```

安装器按 toolchain.lock.json 下载并校验固定安装包，首次安装需联网。之后无需 root 权限编译。首次运行 bash build.sh 还会自动下载 Butano；工具链与 Butano 都准备好后，后续编译可以离线运行。

输出为 dist/sango_gba.gba，检查报告为 dist/build_report.json。默认使用 4 个编译任务，可通过 `BUILD_JOBS=2 bash build.sh` 调整。

不要在含空格的路径中原生构建。已安装 devkitARM 的开发者可设置 DEVKITARM 使用现有后端；本项目固定环境和 CI 验证使用 Wonderful Toolchain。

运行主机测试：

```bash
bash tools/test.sh
```

网页包还需要 p7zip-full。运行 `python3 tools/package_release.py` 同时打包 ROM 和网页版本，详见 [网页版说明](WEB.md)。

## Butano 首次下载与缓存

仓库不再携带 Butano 库。运行 `bash build.sh` 时会自动执行 tools/bootstrap_deps.py，从 dependencies.lock.json 指定的固定提交下载归档，验证 SHA256，只展开编译依赖及许可说明，然后应用经校验的 FLASH 存档标记补丁。

- 下载归档缓存：.cache/dependencies/
- 展开后的库：vendor/butano/
- 两个目录均被 Git 和 Docker 构建上下文忽略，不要提交到仓库。
- 后续构建复用本地依赖，不重复下载。可以提前联网运行 `python3 tools/bootstrap_deps.py`，准备好依赖再离线编译。
- 校验失败会停止，不会静默使用其他版本。若损坏归档，移走对应缓存文件后重试；若库目录标记与锁文件/补丁不匹配，先移走 vendor/butano 再重试。
- 本地直接修改下载后的库不会作为项目变更提交。需要长期修改时，应更新明确记录的补丁及其校验值，并重新构建验证。

请通过 build.sh / build.ps1 构建；若直接运行 make，需要先手动执行 tools/bootstrap_deps.py。

## Windows / WSL

在 WSL Ubuntu-22.04 中按上述步骤安装工具链，之后可在 PowerShell 执行：

```powershell
.\build.ps1
# 如使用其他 WSL 发行版：
.\build.ps1 -Distribution Ubuntu-24.04
```

固定工具链要求 x86_64 WSL 环境。Windows 原生环境无需另装 ARM 编译器。

## Docker 编译

安装 Docker Engine 或 Docker Desktop，启动 Linux 容器引擎，确保 Docker Buildx 可用。宿主机不必安装 ARM 工具链、Python 或 make。

Linux / WSL / macOS shell：

```bash
bash tools/build_docker.sh
# 同时执行全部主机回归测试：
bash tools/build_docker.sh --test
# 指定输出目录：
bash tools/build_docker.sh --output ./docker-output
```

不使用包装脚本时，下面的命令也适用于 PowerShell：

```text
docker buildx build --platform linux/amd64 --target artifacts --output type=local,dest=./dist .
```

附加 `--build-arg RUN_TESTS=1` 可在容器中执行测试，`--build-arg BUILD_JOBS=2` 可降低并行度。

结果直接导出为 dist/sango_gba.gba、构建报告和字库许可说明，不需要另行 docker cp。容器自行下载固定 Butano，不使用宿主机 vendor/。容器内部从 /project 编译，不使用宿主机的 build/、工具链缓存或中间 ROM。

Dockerfile 固定 Ubuntu 22.04 amd64 基础镜像摘要，并使用相同工具链锁文件；系统软件包仍来自发行版仓库，不承诺任意时间镜像逐字节一致。首次构建需联网下载基础镜像、系统依赖、工具链和 Butano，之后可复用 Docker 层缓存。

ARM64 宿主机须支持运行 linux/amd64 容器的仿真，例如相应 Docker Desktop 环境；速度可能较慢。这不是原生 ARM64 工具链。

Docker 不自动重建素材；修改素材时先使用 [素材说明](ASSETS.md) 中的脚本生成资源，再重新构建。

参考：[Docker 官方二进制导出说明](https://docs.docker.com/build/building/export/)。
