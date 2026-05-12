# Joypad Tester — PC Engine — Changelog

## v1.0.0 — 2026-05-11

First release. Builds on dshadoff's `PCE_Mouse_Test` baseline,
extended with a labelled main screen and a screensaver matching the
GameCube and GBA testers.

### Highlights

- Live P1..P5 readout — single pad or up to five via multitap.
- PC Engine mouse: `mouse_exists()` flag, per-frame deltas, and a
  running absolute X / Y accumulator. **I** button / right-click
  toggles mouse-decoded mode (same toggle as upstream).
- Idle screensaver: 64x64 joypad logo sprite bouncing in 7-color
  wall-bounce cycle, matching the GameCube and GBA testers.
