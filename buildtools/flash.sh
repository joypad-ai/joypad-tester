#!/usr/bin/env bash
#
# flash.sh — flash a freshly-built ROM to a flashcart over USB.
# Invoked by each console subdir's `make flash` target (host-side: USB
# loaders talk to host hardware, not the Docker build container).
#
#   buildtools/flash.sh <console> <rom-path>
#
# N64 EverDrive (V3 / X-series) + 64drive + SC64 are flashed with
# UNFLoader, which is cloned + built once into buildtools/.unfloader/
# (cached, gitignored). UNFLoader uses libftdi/libusb directly, which
# on macOS works without unloading the Apple FTDI VCP driver and runs
# at full speed (~1s for a small ROM) -- unlike driving the VCP serial
# node, which the EverDrive menu times out on.
#
# To add another EverDrive-equipped console later, add a case below
# wiring its build artifact to the appropriate USB loader.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONSOLE="${1:?usage: flash.sh <console> <rom-path>}"
ROM="${2:?usage: flash.sh <console> <rom-path>}"

if [ ! -f "$ROM" ]; then
    echo "flash: ROM not found: $ROM (build it first)" >&2
    exit 1
fi

ensure_unfloader() {
    local dir="$ROOT/buildtools/.unfloader"
    local bin="$dir/UNFLoader/UNFLoader"
    if [ ! -x "$bin" ]; then
        echo "flash: building UNFLoader (one-time)..."
        command -v brew >/dev/null || { echo "Homebrew required for libftdi/libusb" >&2; exit 1; }
        brew list libftdi >/dev/null 2>&1 || brew install libftdi
        brew list libusb  >/dev/null 2>&1 || brew install libusb
        rm -rf "$dir"
        git clone --depth 1 https://github.com/buu342/N64-UNFLoader.git "$dir"
        make -C "$dir/UNFLoader"
    fi
    UNFLOADER="$bin"
}

case "$CONSOLE" in
    n64)
        ensure_unfloader
        echo "flash: uploading $ROM to N64 flashcart over USB..."
        # -b: basic (non-curses) output so it works headless.
        # -r: upload ROM and run it. Cart must be at the EverDrive menu.
        exec "$UNFLOADER" -b -r "$ROM"
        ;;
    *)
        echo "flash: no USB loader configured for '$CONSOLE'" >&2
        exit 1
        ;;
esac
