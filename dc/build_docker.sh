#!/usr/bin/env bash
# One-shot Docker-based build for the Dreamcast joypad-tester ROM.
# Builds the KallistiOS toolchain image from buildtools/Dockerfile on
# first run (~10-15 min — KOS + dc-chain compiled from source), then
# runs `make` inside it.
#
# Output: build/joypad_tester_dc_v<VERSION>.cdi (selfboot disc image,
# bootable on emulators and real DC hardware). Filename matches the
# GH Actions release artifact shape; VERSION comes from dc/VERSION.

set -euo pipefail

cd "$(dirname "$0")"

IMAGE_TAG="joypad-tester-kos:latest"

case "${1:-build}" in
    build|"")
        if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
            echo "[build_docker.sh] Building KallistiOS toolchain image $IMAGE_TAG (one-time, ~10-15 min)..."
            docker build -t "$IMAGE_TAG" buildtools/
        fi
        mkdir -p build
        docker run --rm \
            -v "$PWD:/workspace" \
            -w /workspace \
            -u "$(id -u):$(id -g)" \
            "$IMAGE_TAG" \
            make
        VER=$(tr -d '[:space:]' < VERSION)
        echo "[build_docker.sh] Built build/joypad_tester_dc_v${VER}.cdi"
        "$(dirname "$0")/../collect.sh" dc || true
        ;;
    clean)
        docker run --rm \
            -v "$PWD:/workspace" \
            -w /workspace \
            -u "$(id -u):$(id -g)" \
            "$IMAGE_TAG" \
            make clean
        ;;
    shell)
        docker run --rm -it \
            -v "$PWD:/workspace" \
            -w /workspace \
            -u "$(id -u):$(id -g)" \
            "$IMAGE_TAG" \
            bash
        ;;
    rebuild-image)
        docker build --no-cache -t "$IMAGE_TAG" buildtools/
        ;;
    *)
        echo "Usage: $0 [build|clean|shell|rebuild-image]" >&2
        exit 1
        ;;
esac
