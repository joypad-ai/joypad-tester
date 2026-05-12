# Joypad Tester — GBA — Changelog

## v1.0.0 — 2026-05-11

First release.

### Highlights

- Standalone joypad tester for the GBA: live 2-column button readout,
  joybus passthrough so the host sees button state as it changes.
- Idle screensaver: Mode-4 page-flipped joypad logo, 7-color
  wall-bounce cycle.
- Flash-cart standalone fallback if no joybus handshake within
  ~3 seconds — works on a flash cart with no GameCube host.
- Joybus loop taken verbatim from Doridian's Joybus-PIO; reset-edge
  detection inside the VBlank busy-wait so host `cmd 0xFF` triggers
  a clean SVC `0x26` re-multiboot within microseconds.
