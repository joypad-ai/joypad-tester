# Joypad Tester — Xbox — Changelog

## v1.0.0 — 2026-06-10

First stable release. Per-port detection of every standard OG Xbox
controller-port peripheral, plus an on-screen mouse cursor.

### What's new since v0.1.0

- **Per-port device detection** for: Duke / S Controller, daisy-chained
  second pad on a controller's expansion port, **DVD Movie Playback Kit**
  IR receiver (XREMOTE), **Memory Unit** (USB MSC, slot + chassis-direct),
  **Voice Communicator / wired headset / mic** (USB Audio Class 0x01 +
  Xbox-specific class 0x78), **Steel Battalion controller**, generic
  USB **keyboard** and **mouse** (boot-protocol + report-protocol).
- **Analog button pressure** for A / B / X / Y / Black / White (0–255),
  read straight off the XID interrupt buffer since SDL2 only exposes the
  digital state.
- **Pressure-driven rumble** — A → left motor, B → right motor, intensity
  follows pressure. Daisy pad gets its own independent rumble.
- **On-screen mouse cursor** — 8×12 arrow with black backing, integrates
  motion in the HID interrupt callback so dx/dy don't drop between frames.
- **Mouse scroll wheel** — parses the HID report descriptor at probe time,
  detects a leading REPORT_ID byte if present, then runs the mouse in
  report-protocol mode so the wheel byte is visible. Cumulative
  `WheelTotal` shown in the mouse block.
- **USB hub support** — a controller + keyboard + mouse on a single
  chassis port via a USB hub all render under the same port number.
- **About page** — now reports the actual TV output (cable + region +
  scan mode + Hz) decoded from `XVideoGetEncoderSettings()`, distinct
  from the 640×480 internal render surface.
- **Return to dashboard** — Options menu item, plus the in-game-reset
  combo (Back + Black + LT + RT held ~1 s) using `XLaunchXBE(NULL)`.
- **Screensaver** bumped to 2 minutes, with activity detection across
  pads + keyboards + mice + DVD remote + Steel Battalion.
- **Double-buffered framebuffer** via `MmAllocateContiguousMemoryEx` +
  `XVideoSetFB` — no more flicker on mode transitions.

### Verified on real hardware

Duke / S Controller, daisy-chain, Memory Unit (slot + chassis-direct),
Voice Communicator / mic, DVD Movie Playback Kit, USB keyboard, USB
mouse (incl. composite kbd+mouse over a hub).

### Unverified on real hardware

Steel Battalion controller, Arcade Joystick, and steering wheels share
the XID gamepad code path with the verified Duke / S, so detection is
expected to work; type-label fidelity may need a tweak per model. xemu
0.8.x doesn't emulate any of these three, so they need physical
verification before any per-type display fix.

### Build / distribute

- `default.xbe` for softmod HDD installs (drop into `E:\Apps\Joypad
  Tester\` or any FTP'd app folder).
- `joypad_tester_xbox_v1.0.0.iso` (XISO) for DVD-R burns and softmod
  ISO loaders.
- Built with [nxdk](https://github.com/XboxDev/nxdk) via the
  `xbox/buildtools/Dockerfile` image, which extends
  `ghcr.io/xboxdev/nxdk:latest` with `NXDK_USB_ENABLE_MSC=y /
  NXDK_USB_ENABLE_UAC=y / NXDK_USB_ENABLE_HID=y` so the MSC, UAC, and
  HID class drivers are baked into the SDK image. Run
  `./xbox/build_docker.sh` to reproduce.

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
