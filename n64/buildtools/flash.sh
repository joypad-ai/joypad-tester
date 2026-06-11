#!/usr/bin/env bash
#
# flash.sh — flash a freshly-built N64 ROM to an EverDrive / 64drive /
# SC64 over USB via UNFLoader. Host-side: USB loaders talk to host
# hardware, not the Docker build container.
#
#   n64/buildtools/flash.sh <rom-path>
#
# UNFLoader is cloned + built once into buildtools/.unfloader/ (cached,
# gitignored). It uses libftdi / libusb directly, which on macOS runs
# at full speed (~1s for a small ROM) without unloading the Apple FTDI
# VCP driver — unlike driving the VCP serial node, which the EverDrive
# menu times out on.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROM="${1:?usage: flash.sh <rom-path>}"

if [ ! -f "$ROM" ]; then
    echo "flash: ROM not found: $ROM (build it first)" >&2
    exit 1
fi

UNFLOADER_DIR="$ROOT/buildtools/.unfloader"
UNFLOADER="$UNFLOADER_DIR/UNFLoader/UNFLoader"

if [ ! -x "$UNFLOADER" ]; then
    echo "flash: building UNFLoader (one-time)..."
    command -v brew >/dev/null || { echo "Homebrew required for libftdi/libusb" >&2; exit 1; }
    brew list libftdi >/dev/null 2>&1 || brew install libftdi
    brew list libusb  >/dev/null 2>&1 || brew install libusb
    rm -rf "$UNFLOADER_DIR"
    git clone --depth 1 https://github.com/buu342/N64-UNFLoader.git "$UNFLOADER_DIR"
    make -C "$UNFLOADER_DIR/UNFLoader"
fi

echo "flash: uploading $ROM to N64 flashcart over USB..."
# -b: basic (non-curses) output so it works headless.
# -r: upload ROM and run it. Cart must be at the EverDrive menu.
exec "$UNFLOADER" -b -r "$ROM"
