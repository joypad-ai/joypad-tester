#!/usr/bin/env bash
# Simple helper script to build through Docker to avoid installing devkitpro toolchain

set -euo pipefail

# help print function
print_help () {
    echo "You need to specify the command to run: wii, gamecube or clean"
    exit 1
}

# Check only argument is provided
if [ $# -ne 1 ]
  then
    print_help
fi

# Sync the GBA tester payload from the sibling subdir if it's newer.
# Docker only mounts $PWD into the container, so the Makefile's
# gba-payload-sync rule can't see ../gba/ from inside; do it host-side
# before the build kicks off. (CI runs `docker run` with the workspace
# root mounted and the Makefile rule handles it there directly.)
GBA_PAYLOAD_SRC="$PWD/../gba/build/tester/tester_payload.c"
GBA_PAYLOAD_OUT="$PWD/ppc/gba_payload.c"
if [ -f "$GBA_PAYLOAD_SRC" ] && [ "$GBA_PAYLOAD_SRC" -nt "$GBA_PAYLOAD_OUT" ]; then
    echo "[gcn/build_docker.sh] vendor ../gba/build/tester/tester_payload.c -> ppc/gba_payload.c"
    sed 's/joypad_payload/gba_payload/g' "$GBA_PAYLOAD_SRC" > "$GBA_PAYLOAD_OUT"
fi

# Base docker command
COMMON_DOCKER_ARGS=(-v "$PWD":/project -w /project -u "$(id -u "${USER}")":"$(id -g "${USER}")" ghcr.io/extremscorner/libogc2:latest make)

# Choose operation
case $1 in

  wii)
    docker run -e "TARGET_CONSOLE=wii" "${COMMON_DOCKER_ARGS[@]}"
    ;;

  gamecube)
     docker run -e "TARGET_CONSOLE=gamecube" "${COMMON_DOCKER_ARGS[@]}"
    ;;

  clean)
     rm -rf build_wii
     rm -rf build_gamecube
     rm -rf -- *.elf
     rm -rf -- *.dol
    ;;

  *)
    print_help
    ;;
esac
