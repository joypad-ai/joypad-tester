#!/usr/bin/env bash
# One-shot Docker-based build for the 3DO joypad-tester ROM. Builds
# the toolchain image from buildtools/Dockerfile on first run
# (clones trapexit/3do-devkit at a pinned commit), then runs make
# inside it with our src/ overlaid on the devkit's.

set -euo pipefail

cd "$(dirname "$0")"

IMAGE_TAG="joypad-tester-3do:latest"
TARGET="joypad-tester"

case "${1:-build}" in
    build|"")
        if ! docker image inspect "$IMAGE_TAG" >/dev/null 2>&1; then
            echo "[build_docker.sh] Building 3DO toolchain image $IMAGE_TAG (one-time)..."
            docker build --platform=linux/amd64 -t "$IMAGE_TAG" buildtools/
        fi
        mkdir -p build .wine-home
        # Run make inside the container. The image pre-baked /opt/3do-devkit
        # with empty src/<subdirs>/; we drop our main.cpp into the devkit's
        # src/, build, and copy the resulting iso back to our build/.
        docker run --rm \
            --platform=linux/amd64 \
            -v "$PWD:/work" \
            -u "$(id -u):$(id -g)" \
            -e HOME=/work/.wine-home \
            "$IMAGE_TAG" \
            bash -c "
                set -e

                # Asset pipeline (host-side art -> 3DO-format files
                # inside the devkit's takeme/ tree before the iso step
                # bundles them up):
                #   assets/banner.png   -> banner.png   (devkit Makefile
                #                                        runs 3it to-banner
                #                                        on this for the
                #                                        BIOS splash screen)
                #   assets/logo_64.png  -> takeme/LogoCel.cel (via 3it
                #                                              to-cel; main.cpp
                #                                              LoadCel's it)
                cp /work/assets/banner.png /opt/3do-devkit/banner.png
                3it to-cel -b 4 --coded true --transparent white \
                    -o /opt/3do-devkit/takeme/LogoCel.cel \
                    /work/assets/logo_64.png

                # Stage our source over the devkit's example launcher.
                cp /work/src/main.cpp /opt/3do-devkit/src/main.cpp

                cd /opt/3do-devkit
                # make NAME=... (not env-prefix) -- env vars are
                # shadowed by the Makefile's own 'NAME = helloworld'
                # assignment, but command-line overrides win.
                #
                # 'banner' is a phony target the default goal doesn't
                # depend on, so to actually swap the BannerScreen file
                # from the devkit's default ('FICTIONAL DEVELOPER /
                # BOGUS TITLE') to our own we have to invoke it
                # explicitly. Order matters: banner first so it lands
                # in takeme/ before the iso composer reads the tree.
                make NAME=$TARGET banner
                make NAME=$TARGET
                cp iso/$TARGET.iso /work/build/$TARGET.iso
            "
        echo "Built build/$TARGET.iso"
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
