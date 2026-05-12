# Joypad Tester — GameCube — Changelog

## v1.0.0 — 2026-05-11

Initial release.

### What it does

- Live multi-port display for all four SI ports simultaneously, no
  active-port toggling.
- Native GameCube controller support (wired + WaveBird): buttons,
  D-pad, analog stick + C-stick, analog triggers, rumble.
- N64 controller support via passive adapter (libogc2 SI detection):
  buttons, D-pad, analog stick. Accessory paks detected and reported:
  Memory Pak, Rumble Pak, Transfer Pak, Bio Sensor (with BPM
  readout), Snap Station. Rumble actuation on hold-A for both GCN's
  built-in motor and N64's Rumble Pak.
- N64 mouse style detection.
- GameCube ASCII keyboard support: detects `SI_GC_KEYBOARD`, polls
  with the 3-byte cmd `0x54`, maps scancodes to key labels using the
  joypad-os reference table.
- GBA detection and multiboot upload over the official GameCube GBA
  Link Cable. After upload the GBA boots into the Joypad Tester GBA
  tester ROM, so users see button state on both the GBA's own screen
  and the GameCube's per-port readout.
- Sticky-cache disconnect handling: `kbd_chan` / `wheel_chan` /
  `gc_chan` ride out the SI transients libogc walks through during
  a controller pull (especially the GBA), so the port label doesn't
  flicker through Keyboard / Wheel / GCN ghosts before settling on
  None.
- Idle screensaver after 30s: bouncing joypad logo bitmap with a
  7-color cycle (red / green / yellow / blue / magenta / cyan /
  white) on each wall bounce. Any input wakes back to the live view.

### Build

- Per-target Docker build: `./build_docker.sh gamecube` produces
  `joypad-tester-gamecube.dol`; `./build_docker.sh wii` produces
  `joypad-tester-wii.dol`. Toolchain image:
  `ghcr.io/extremscorner/libogc2:latest`.
- `opening.bnr` generated from `branding/banner.png` via
  `buildtools/make_banner.py`.
- Screensaver logo header generated from `branding/logo.png` via
  `buildtools/make_logo.py`.
- GBA payload (the on-GBA tester ROM byte array) auto-synced from
  `../gba/build/tester/tester_payload.c` to `ppc/gba_payload.c` by
  `build_docker.sh` and the Makefile, with the symbol renamed
  `joypad_payload` -> `gba_payload` so it doesn't collide with
  anything else in this tree.

### Release artifacts

- `joypad_tester_v1.0.0_gamecube.dol` (drop onto a Swiss SD card)
- `joypad_tester_v1.0.0_wii.dol` (Wii via the Homebrew Channel)
- `opening.bnr` (Swiss boot banner)
