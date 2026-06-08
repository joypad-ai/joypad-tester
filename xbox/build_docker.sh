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

IMAGE_TAG="ghcr.io/xboxdev/nxdk:latest"
JOBS="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"

case "${1:-build}" in
    build|"")
        docker pull --platform=linux/amd64 "$IMAGE_TAG" >/dev/null 2>&1 || true
        docker run --rm \
            --platform=linux/amd64 \
            -v "$PWD:/usr/src/app" \
            -w /usr/src/app \
            -u "$(id -u):$(id -g)" \
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
            "$IMAGE_TAG" \
            make clean
        ;;
    *)
        echo "usage: $0 [build|clean]" >&2
        exit 1
        ;;
esac
