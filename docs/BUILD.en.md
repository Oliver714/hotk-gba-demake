# Building on Linux, WSL, and Docker

[简体中文](BUILD.md) · [English README](../README.en.md)

Run all commands from the repository root. A normal build uses `generated/` directly and does not require the original game, source artwork, asset converters, or ffmpeg.

## Native Linux build

The pinned toolchain currently supports **Linux x86_64**. On Ubuntu 22.04 or a Debian-based distribution:

```bash
sudo apt-get update
sudo apt-get install -y ca-certificates python3 make g++ xz-utils
sudo mkdir -p /opt/wonderful
sudo chown "$(id -u):$(id -g)" /opt/wonderful
python3 tools/install_toolchain.py
bash build.sh
```

The installer downloads and verifies the packages pinned in `toolchain.lock.json`. Compilation does not need root privileges after setup. The first `bash build.sh` also downloads Butano. Once the toolchain and Butano are available, subsequent builds can run offline.

The ROM is `dist/sango_gba.gba`; its validation report is `dist/build_report.json`. Builds use four parallel jobs by default; override this with, for example, `BUILD_JOBS=2 bash build.sh`.

Build in a path without spaces. Developers with devkitARM installed can use its backend through `DEVKITARM`, but the project's pinned setup and CI validation use Wonderful Toolchain.

Run host tests separately:

```bash
bash tools/test.sh
```

Web packaging also requires `p7zip-full`. Run `python3 tools/package_release.py` to package the ROM and web edition; see the [web guide](WEB.en.md).

For the English edition, use `GAME_LANGUAGE=en bash build.sh`, `GAME_LANGUAGE=en bash tools/test.sh`, and `python3 tools/package_release.py --language en`. See the [English edition guide](ENGLISH.md) for output names and save isolation.

## Butano download and cache

Butano is not checked into this repository. `bash build.sh` runs `tools/bootstrap_deps.py`, which downloads the archive for the commit pinned in `dependencies.lock.json`, verifies its SHA256, extracts the required library and license files, and applies a checked FLASH save-marker patch.

- Download cache: `.cache/dependencies/`.
- Extracted library: `vendor/butano/`.
- Both directories are excluded from Git and the Docker build context. Do not commit them.
- Subsequent builds reuse the cached dependency. You can run `python3 tools/bootstrap_deps.py` in advance to prepare for offline builds.
- Verification failures stop the build instead of silently selecting another version. Remove a damaged archive or its cache file and retry. If the installed dependency's marker or patch does not match, remove `vendor/butano/` and retry.
- Local edits inside the downloaded library are not recorded as project changes. To retain a dependency change, add an explicit patch and its checksum, then rebuild and validate.

When running `make` directly instead of `build.sh` or `build.ps1`, run `tools/bootstrap_deps.py` first.

## Windows / WSL

Install the toolchain inside WSL Ubuntu-22.04 using the Linux steps above, then run in PowerShell:

```powershell
.\build.ps1
# To use another WSL distribution:
.\build.ps1 -Distribution Ubuntu-24.04
```

The pinned toolchain runs inside x86_64 WSL. No native Windows ARM compiler is required. Add `-Language en` for the English edition.

## Docker build

Install Docker Engine or Docker Desktop, enable Linux containers, and make sure Docker Buildx is available. The host does not need an ARM compiler, Python, or make.

From a Linux, WSL, or macOS shell:

```bash
bash tools/build_docker.sh
# Also run the full host regression suite:
bash tools/build_docker.sh --test
# Choose an output directory:
bash tools/build_docker.sh --output ./docker-output
```

Without the wrapper script, use the following command, which also works in PowerShell:

```text
docker buildx build --platform linux/amd64 --target artifacts --output type=local,dest=./dist .
```

Add `--build-arg RUN_TESTS=1` to run tests inside the container, `--build-arg BUILD_JOBS=2` to reduce parallelism, or `--build-arg GAME_LANGUAGE=en` to build English artifacts.

The export contains `dist/sango_gba.gba`, the build report, and license notices; no `docker cp` step is needed. The container downloads the pinned Butano version rather than using the host's `vendor/`. It builds in `/project` and does not reuse the host's `build/` directory or any intermediate ROM.

The Dockerfile pins an Ubuntu 22.04 amd64 base image by digest and uses the same toolchain lock file. System packages still come from distribution repositories, so image builds are not guaranteed to be byte-for-byte identical across dates. The first build requires network access for the base image, system packages, toolchain, and Butano; later builds can reuse Docker layers.

ARM64 hosts need support for emulating linux/amd64 containers, such as Docker Desktop, and may build more slowly. This is not a native ARM64 toolchain.

Docker does not regenerate assets automatically. Use the scripts in the [asset guide](ASSETS.en.md) to update generated resources before rebuilding.

Reference: [Docker build exporters](https://docs.docker.com/build/building/export/).
