#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname -- "${BASH_SOURCE[0]}")/.."
output=./dist
run_tests=0
while (( $# )); do
    case "$1" in
        --test) run_tests=1; shift ;;
        --output)
            if (( $# < 2 )) || [[ -z "$2" ]]; then echo '--output requires a directory' >&2; exit 2; fi
            output="$2"; shift 2 ;;
        *) echo 'Usage: bash tools/build_docker.sh [--test] [--output DIR]' >&2; exit 2 ;;
    esac
done
command -v docker >/dev/null || { echo 'Install Docker with Buildx first.' >&2; exit 1; }
docker info >/dev/null 2>&1 || { echo 'Start the Docker engine (Linux containers) first.' >&2; exit 1; }
docker buildx build --platform linux/amd64 --target artifacts \
    --build-arg "RUN_TESTS=$run_tests" --build-arg "BUILD_JOBS=${BUILD_JOBS:-4}" \
    --output "type=local,dest=$output" .
printf 'ROM: %s/sango_gba.gba\n' "$output"
