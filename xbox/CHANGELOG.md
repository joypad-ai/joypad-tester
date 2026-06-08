# Joypad Tester — Xbox — Changelog

## v0.1.0 — 2026-06-08

First release. Brings the joypad-tester family to the OG Xbox.

### Highlights

- **Controller tester** — every connected pad on ports A–D shows up
  with its title, vendor / product ID, the full button mask
  (A / B / X / Y / Black / White / Back / Start / L3 / R3 / D-pad),
  both sticks (signed 16-bit), and both triggers (0–255).
  Hot-plug supported. Holding **A** rumbles the pad for as long as
  the button is held.
- **Memory Unit detection** — probes `\Device\Memunit0..7\Partition1`,
  mounts each as a per-MU drive letter, and surfaces free / total KB
  in the per-port slot row (`MU 7234K/8192K`). Re-probes once per
  second so hotplugged MUs pick up automatically. Read-only — no
  file ops in this release.

### Build / distribute

- `default.xbe` for softmod HDD installs.
- `joypad_tester_xbox_v0.1.0.iso` (XISO) for DVD-R burns and
  softmod ISO loaders.
- Built with [nxdk](https://github.com/XboxDev/nxdk) (clang + lld +
  pbkit + SDL2) via the upstream `ghcr.io/xboxdev/nxdk:latest`
  Docker image — no local toolchain install needed.

### Known caveats

- MU device-path convention (`\Device\Memunit%d\Partition1`) and the
  port/slot → MU index mapping (`port * 2 + slot`) are
  community-documented rather than from a kernel header; both still
  want hardware confirmation. Adjust `xbox/src/ports/mu.c` if the
  empirical mapping differs.
- Analog button pressure (the A/B/X/Y/Black/White 0..255 analog
  range) isn't yet surfaced; SDL2's gamecontroller layer only
  exposes the digital state.
