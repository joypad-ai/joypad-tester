#!/usr/bin/env bash
# One-shot Docker-based build for the N64 joypad-tester ROM. Pulls the
# LibDragon toolchain image and runs `make` inside it -- no local
# install of the mips64-elf cross-compiler needed.

set -euo pipefail

cd "$(dirname "$0")"

IMAGE_TAG="joypad-tester-n64:latest"

case "${1:-build}" in
    build|"")
        if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
            echo "[build_docker.sh] Building N64 toolchain image $IMAGE_TAG (one-time, pulls libdragon base)..."
            docker build --platform=linux/amd64 -t "$IMAGE_TAG" buildtools/
        fi
        mkdir -p build
        docker run --rm \
            --platform=linux/amd64 \
            -v "$PWD:/work" \
            -u "$(id -u):$(id -g)" \
            "$IMAGE_TAG" \
            make
        echo "Built build/joypad-tester.z64"
        ;;
    clean)
        rm -rf build/
        ;;
    rebuild-image)
        docker build --no-cache --platform=linux/amd64 -t "$IMAGE_TAG" buildtools/
        ;;
    *)
        echo "Usage: $0 [build|clean|rebuild-image]" >&2
        exit 1
        ;;
esac
