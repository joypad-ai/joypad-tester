#!/usr/bin/env bash
#
# build_docker.sh -- one-shot Docker build for the OG Xbox tester.
# Uses the upstream nxdk image (ghcr.io/xboxdev/nxdk:latest), which
# ships the full toolchain prebuilt; no local nxdk install needed.
#
# Pinning: upstream tags :latest, :debug, :lto, plus per-commit
# git-<sha> tags. We track :latest for now and can pin to a git-SHA
# tag if a regression shows up.
#
# Platform: the upstream image is amd64-only. We pin --platform so
# Apple Silicon hosts pull + run via Rosetta/qemu instead of failing
# with "no matching manifest"; on ubuntu-latest CI it's a no-op.

set -euo pipefail

cd "$(dirname "$0")"

IMAGE_TAG="joypad-tester-nxdk:latest"
UPSTREAM_TAG="ghcr.io/xboxdev/nxdk:latest"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

# Compute the build stamp on the host. The Docker mount is xbox/ only
# (no .git), so we have to inject the SHA via env instead of letting
# make's in-container git lookup fall back to "nogit". The line 17
# `cd` above has put us inside xbox/, so the repo root is one level up.
HOST_BUILD_SHA="$(git -C .. rev-parse --short=7 HEAD 2>/dev/null || echo nogit)"
if git -C .. diff --quiet 2>/dev/null; then
    HOST_BUILD_DIRTY=""
else
    HOST_BUILD_DIRTY="-dirty"
fi
export HOST_BUILD_SHA HOST_BUILD_DIRTY

case "${1:-build}" in
    build|"")
        if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
            echo "[build_docker.sh] Building $IMAGE_TAG (one-time, ~5 min on Apple Silicon via Rosetta)..."
            docker pull --platform=linux/amd64 "$UPSTREAM_TAG" >/dev/null 2>&1 || true
            docker build --platform=linux/amd64 -t "$IMAGE_TAG" buildtools/
        fi
        docker run --rm \
            --platform=linux/amd64 \
            -v "$PWD:/usr/src/app" \
            -w /usr/src/app \
            -u "$(id -u):$(id -g)" \
            -e HOST_BUILD_SHA \
            -e HOST_BUILD_DIRTY \
            "$IMAGE_TAG" \
            make -j"$JOBS"
        "$(dirname "$0")/../collect.sh" xbox || true
        ;;
    clean)
        docker run --rm \
            --platform=linux/amd64 \
            -v "$PWD:/usr/src/app" \
            -w /usr/src/app \
            -u "$(id -u):$(id -g)" \
            -e HOST_BUILD_SHA \
            -e HOST_BUILD_DIRTY \
            "$IMAGE_TAG" \
            make clean
        ;;
    *)
        echo "usage: $0 [build|clean]" >&2
        exit 1
        ;;
esac
