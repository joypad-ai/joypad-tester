# Joypad Tester — Nintendo 64 — Changelog

## v1.0.2 — 2026-05-20

RandNet keyboard text-input polish.

### Highlights

- **Keyboard input drives the idle screensaver** — typing both keeps
  it away and wakes it. The keyboard is scanned every frame from the
  main loop (not just inside the tester mode), so it works even while
  the screensaver is up (the keyboard isn't a libdragon-classified
  controller, so the pad/analog idle checks were blind to it).
- **Ghost/overflow scans don't type.** When the keyboard flags an
  unreliable scan — 4+ keys, or a matrix ghost from three keys
  forming a rectangle (e.g. F1+F2+F3) — the typed line ignores it,
  matching how real N64 keyboard software treats the overflow bit.
  The `Keys:`/`(4+)` diagnostic still shows the raw state.
- **Typematic key repeat.** Holding a character key or Backspace
  repeats it after a ~0.5s delay (~20/s), like a real keyboard; the
  most-recently-pressed key repeats. Modifiers, locks, F-keys and
  arrows don't repeat.
- **Caps Lock / Num Lock are real toggles.** A press flips the lock,
  the keyboard's physical LED reflects the lock state (not the
  momentary press), and it persists.
- **Correct letter case.** Letters render upper/lowercase by
  Shift XOR Caps Lock, like a real keyboard. (Shifted *symbol*
  variants aren't remapped — the n64brew key matrix only documents
  each key's base function.)
- **Enter clears the typed line; the line scrolls when full**
  (drops the oldest char) instead of freezing.

## v1.0.1 — 2026-05-20

Adds RandNet keyboard (RND-001) support to the controller tester.
The keyboard is the Japan-only RANDnet 64DD peripheral; libdragon
only ships its Joybus identifier, so the read protocol is hand-rolled
over `joybus_exec_cmd`.

### Highlights

- **RandNet keyboard**: a port reporting identifier `0x0002` is
  labelled `Keybd / RandNet Keyboard` and polled with Joybus command
  `0x13`. The full 85-key matrix is decoded (transcribed from the
  n64brew wiki "Keyboard" map, cross-checked against meeq's
  KeyboardTest-N64), so the tester shows the **named keys** being held
  (`A`, `Space`, `Shift`, `Enter`, `F5`, …) plus a live **`Typed:`**
  line with a blinking cursor — Shift upper-cases letters, BackSpace
  deletes. Caps / Num lock indicators, and the keyboard's physical
  Power / Caps / Num Lock **LEDs** are driven over the wire.
  Unverified on real RND-001 hardware (no emulator emulates it), but
  protocol + scancodes are correct-by-source.

## v1.0.0 — 2026-05-19

First public release. Joint v1.0.0 cut alongside the other consoles.
Tests every Joybus device class that lands on an N64 controller port:
N64 pads, GameCube pads (passive adapter), the N64 Mouse, the Voice
Recognition Unit, every controller-pak accessory (Rumble, Memory,
Transfer Pak, Bio Sensor, Snap Station), and a GBA in JOYBUS mode
via the GameCube / GBA link cable. Built on LibDragon trunk and
meeq's now-upstreamed joypad subsystem, with a mode-switcher
scaffold copied from the dc/ subdir.

### Highlights

- **Controller Tester** (landing screen): live per-port grid with a
  GC/DC-style layout — Type + Pak + Rumble header, Stick / C-Stick /
  L-Trig / R-Trig analog row, A/B/X/Y/L/R/Z/Start button row,
  D-pad + C-pad row. Held buttons render yellow against dim unheld
  labels; port headers turn green when connected. Layout is per-
  controller-aware: **N64 pads** drop the GCN-only `C-Stick` and
  `X/Y` buttons, **GameCube pads** drop the N64-only digital `C-pad`
  (since GCN uses the analog C-Stick instead).
  Special-case device rows:
  - **N64 Mouse**: running absolute position + per-frame delta in
    place of the analog/buttons rows.
  - **N64 VRU / GBA**: identifier-aware labels alongside the Type
    column even when libdragon's `joypad_style_t` returns `NONE`.
  - **Bio Sensor**: live BPM streaming via meeq's pulse-decoder
    algorithm (ported onto libdragon's sync `joybus_accessory_read`),
    plus a `Pulse: PULSING / Resting` indicator — rendered above the
    pad's regular button rows, not in place of them.
  - **Transfer Pak**: inline GB cartridge header peek (title +
    cart_type / rom_size / ram_size codes).
  - **GBA in JOYBUS mode**: Kawasedo handshake + multiboot upload
    fires automatically as soon as the link cable is detected (no
    button press needed, matches the gcn tester). A `-2` ready
    timeout backs off and auto-retries instead of parking in
    `BootFail`. Detection is sticky across libdragon's per-frame
    identifier flicker, and the boot uses a byte-order-agnostic
    direct Joybus probe. Once booted, REG_KEYINPUT is decoded onto
    the same row.
  - **Rumble**: every pad that supports rumble shakes its own motor
    while *its own* A button is held — covers both N64 pads with a
    Rumble Pak attached and GameCube pads with their built-in motor.
    No separate rumble-test mode.
- **Controller Pak Browser**: per-port note list via libdragon's
  `mempak_*` (name, blocks, region, game code).
- **GB Camera Viewer**: detects a GB Camera cartridge through a
  Transfer Pak, drives the GB Camera ASIC's capture-state register,
  reads the 128×112 raw planar frame, decodes to RGBA32, and renders
  the captured photo on screen in the authentic 4-shade DMG-green
  palette. A button triggers a fresh capture.
- **Snap Station Test**: protocol exerciser for builders of homebrew
  Snap Station replicas. Detects the accessory, probes the device,
  and exposes meeq's full Joybus state-machine command map
  (Pre-Save / Post-Save / Reset / Pre-Roll / Capture / Post-Roll +
  Read State).
- **About**: version + credits + repo URL.
- **Idle screensaver**: after 30 s of zero input across all ports,
  switches to a bouncing 76×64 Joypad logo silhouette in the
  canonical 7-color wall-bounce cycle (matches gcn / pce / gba / dc /
  3do). Auto-scales 2× horizontally to compensate for N64's
  anamorphic 320×240 framebuffer. Wakes on any pad button, any
  Mouse delta (no deadzone), or any GBA button via the link cable.
- **Options menu** scaffold (dc-style): Start+Down opens the modal
  in Tester mode (so Start alone stays a controller input there);
  any other screen opens with bare Start. D-pad navigates, A
  confirms, Start closes.
- **Colored text rendering** via a small `ui/text.{c,h}` helper on
  top of `graphics_draw_text` — per-segment foreground colour
  through `graphics_make_color`, surface-format-agnostic.

Full feature breakdown + build / loading instructions in
[`n64/README.md`](https://github.com/joypad-ai/joypad-tester/blob/main/n64/README.md).
