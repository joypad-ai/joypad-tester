# Joypad Tester — GameCube — Changelog

## v0.2.0 — 2026-05-11

First release under the new short-codename convention. The subdir is
now `gc/` (was `gamecube/`) and the release tag is `gc-v0.2.0` (was
`gamecube-v0.1.0`). The historical `gamecube-v0.1.0` tag/release stays
intact as a frozen snapshot of the old layout — future releases all
use the `gc-` prefix.

### Fixed

- GBA disconnect no longer ghost-detects as a regular GameCube
  controller. libogc's `SI_GetType` walks through transient values
  during cable removal that mask to `SI_TYPE_GC` (with or without
  `SI_GC_STANDARD` set); the bare type-match fallback was latching
  those as "GCN". Added a `gc_chan[]` sticky cache parallel to
  `kbd_chan` / `wheel_chan`, gated on `SI_GC_STANDARD` + no
  `NO_RESPONSE`, so transient reads can't flip the port label.

### Changed

- Subdir rename `gamecube/` -> `gc/` to match the short-codename
  convention used by `gba/`, `pce/`, and future consoles. Tag prefix
  shifts from `gamecube-v*` to `gc-v*`. `TARGET_CONSOLE=gamecube|wii`
  inside the libogc build is unchanged (that's the libogc internal
  platform name, not our subdir codename).

## v0.1.0 — 2026-05-07

Initial release.

- Live multi-port display for all four SI ports simultaneously.
- Native GameCube controller support: buttons, D-pad, analog stick + C-stick, analog triggers, rumble.
- N64 controller support via passive adapter (libogc2 SI detection): A/B/Z/Start, D-pad, L/R, C-buttons, analog stick.
- N64 accessory detection: Memory Pak, Rumble Pak, Transfer Pak, Bio Sensor, Snap Station — using libdragon's probe sequence.
- Rumble actuation on hold-A: built-in motor for GCN, Rumble Pak via accessory write for N64.
- N64 mouse style detection.
- GameCube ASCII keyboard support: detects `SI_GC_KEYBOARD`, polls with 3-byte cmd `0x54`, maps scancodes to key labels (A/B/C, F1-F12, modifiers, arrows, etc.) using the joypad-os reference table.
- Boot banner (`opening.bnr`) for Swiss-GC display, generated from `branding/banner.png` at build time.
- Per-console build via Docker (`./build_docker.sh gamecube|wii`) using `ghcr.io/extremscorner/libogc2`.
