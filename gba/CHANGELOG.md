# Joypad Tester — GBA — Changelog

## v1.0.0 — 2026-05-11

Initial release. Two ROM variants built from one source tree:

### Tester (`tester_mb.gba`) — the public release

Standalone joypad tester for flash carts and for the GameCube host's
multiboot pipeline.

- 30-column on-GBA console with live button state in a 2-column
  layout (A / B / L / R / Start / Select / dpad).
- Idle screensaver: 64x54 joypad logo bouncing in Mode-4 page-flipped
  framebuffer with a 7-color cycle on each wall bounce, matching the
  GameCube tester's screensaver.
- Standalone fallback: if no joybus handshake is detected within
  ~3 seconds, the console reports button state from `REG_KEYINPUT`
  directly, so the ROM works on a flash cart even without a host.

### Eyes (`joypad_mb.gba`) — not released publicly

Same joybus loop as the tester variant, but rendering the eyes
overlay (cartoon eyes + emotion state machine) instead of the
tester console.

- Designed as a firmware payload for joypad-os's RP2040 GameCube
  controller adapter; joypad-os keeps its own independent copy.
- Builds in `gba/build/joypad/` for source-tree inspection but is
  not a Release attachment.

### Shared

- Joybus handshake + main loop taken verbatim from Doridian's
  [Joybus-PIO](https://github.com/Doridian/Joybus-PIO) `gba/source/main.c`
  (2023, MIT). Modifying it breaks the cable's level-shifter MCU,
  so both variants share the loop unchanged.
- Hot-swap reset: `REG_JOYCNT.RST` polled inside the VBlank busy-wait
  so a host `cmd 0xFF` triggers `SystemCall(0x26)` within
  microseconds. Enables clean host-reboot re-multiboot.
- Two-variant Makefile dispatched via `VARIANT=` env var. `make` /
  `make joypad` / `make tester` / `make clean`.

### Build

- devkitPro / devkitARM via `docker run ... devkitpro/devkitarm:latest
  make`. Outputs in `build/joypad/` (eyes) and `build/tester/`
  (tester). Both `*_mb.gba` and `*_payload.c` (C-array embed form)
  are committed so consumers using `gba/` as a submodule don't need
  devkitARM unless they're modifying source.

### Release artifacts

- `joypad_tester_v1.0.0.gba` (= tester variant, renamed at release
  staging for flash-cart drop-in legibility)
