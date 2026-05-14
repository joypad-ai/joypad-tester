#!/usr/bin/env bash
# One-shot Docker-based build for the NUON joypad-tester ISO. Builds
# the toolchain image from buildtools/Dockerfile on first run (a thin
# i386/debian:bullseye-slim base with `make` and `genisoimage`), then
# sources cubanismo/nuon-sdk's env.sh inside it and runs `make`.

set -euo pipefail

cd "$(dirname "$0")"

IMAGE_TAG="joypad-tester-nuon:latest"

case "${1:-build}" in
    build|"")
        if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
            echo "[build_docker.sh] Building NUON toolchain image $IMAGE_TAG (one-time)..."
            docker build --platform=linux/386 -t "$IMAGE_TAG" buildtools/
        fi
        mkdir -p build

        # Locate the cubanismo/nuon-sdk checkout. Prefer
        # ~/git/nuon-sdk (a developer-facing fork checkout where SDK
        # edits land) so changes to env.sh, the bless/ tools, the
        # linux/i386 binaries, etc. flow through every build
        # immediately. Fall back to a shallow clone in .nuon-sdk/ for
        # fresh checkouts of this repo.
        if [ -d "$HOME/git/nuon-sdk/.git" ]; then
            NUON_SDK_DIR="$HOME/git/nuon-sdk"
            echo "[build_docker.sh] Using SDK at $NUON_SDK_DIR"
        else
            NUON_SDK_DIR="$PWD/.nuon-sdk"
            if [ ! -d "$NUON_SDK_DIR/.git" ]; then
                echo "[build_docker.sh] Cloning cubanismo/nuon-sdk (one-time, ~55MB)..."
                git clone --depth 1 https://github.com/cubanismo/nuon-sdk.git "$NUON_SDK_DIR"
            fi
        fi

        # The container runs as the host user so output files land
        # owned correctly. env.sh exports VMLABS / VMBLESSDIR /
        # VMHOSTARCH / BUILDHOST=LINUX and prepends the SDK bin dirs
        # to PATH; we source it then invoke our Makefile.
        docker run --rm \
            --platform=linux/386 \
            -v "$PWD:/work" \
            -v "$NUON_SDK_DIR:/sdk:ro" \
            -u "$(id -u):$(id -g)" \
            "$IMAGE_TAG" \
            bash -c "
                set -e
                . /sdk/env.sh
                cd /work
                make
            "
        echo "Built build/joypad-tester.iso"
        ;;
    clean)
        rm -rf build/
        ;;
    rebuild-image)
        docker build --no-cache --platform=linux/386 -t "$IMAGE_TAG" buildtools/
        ;;
    *)
        echo "Usage: $0 [build|clean|rebuild-image]" >&2
        exit 1
        ;;
esac
