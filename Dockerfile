# Build with --platform linux/amd64: the locked tools are x86_64.
FROM ubuntu:22.04@sha256:98510ac07bb4545e389be502a8918b385c018bfe4ceeb5ac7e0375c782268b04 AS toolchain
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates make g++ python3 xz-utils p7zip-full \
    && apt-get clean
WORKDIR /project
COPY toolchain.lock.json ./
COPY tools/install_toolchain.py tools/install_toolchain.py
RUN python3 tools/install_toolchain.py --cache /tmp/toolchain-cache
ENV WONDERFUL_TOOLCHAIN=/opt/wonderful

FROM toolchain AS dependencies
COPY dependencies.lock.json ./
COPY dependencies/ dependencies/
COPY tools/bootstrap_deps.py tools/bootstrap_deps.py
RUN python3 tools/bootstrap_deps.py --cache /tmp/dependency-cache

FROM dependencies AS build
COPY . .
ARG RUN_TESTS=0
ARG BUILD_JOBS=4
RUN case "$RUN_TESTS" in 0) ;; 1) bash tools/test.sh ;; *) exit 2 ;; esac \
    && BUILD_JOBS="$BUILD_JOBS" bash build.sh

FROM scratch AS artifacts
COPY --from=build /project/dist/ /

FROM build AS web-build
RUN python3 tools/package_release.py

FROM scratch AS web-artifacts
COPY --from=web-build /project/release/ /
