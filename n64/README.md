# Joypad Tester — Nintendo 64

A test ROM for the Nintendo 64 that shows you, live and on screen,
exactly what every controller and accessory plugged into your N64 is
doing. Part of the multi-console [Joypad Tester](../README.md).

## What it does

Plug stuff into your four controller ports — N64 pads, GameCube pads
(via passive adapter), an N64 Mouse, a Voice Recognition Unit, any
controller-pak accessory, even a Game Boy Advance over a GameCube /
GBA link cable — and you get a live readout per port:

- **Buttons, sticks, triggers, D-pad, C-buttons**. Held inputs light
  up; layout adapts so N64 pads show C-buttons and GameCube pads
  show their C-Stick + X/Y face buttons.
- **The N64 Mouse** swaps the analog row for live absolute position
  + per-frame delta.
- **Rumble**: holding **A** rumbles that port's motor — works with
  an N64 Rumble Pak or a GameCube pad's built-in motor.
- **Memory Pak / Controller Pak**: a built-in browser lists every
  save note on the cart (name, blocks, region, game code).
- **Transfer Pak**: shows the inserted Game Boy cart's title + size
  codes inline. Pop in a **GB Camera** and a separate viewer mode
  captures a photo and renders it in DMG-green.
- **Bio Sensor**: live BPM + a `PULSING / Resting` indicator. Yes,
  the heart-rate sensor from *Tetris 64* still works.
- **VRU**: detected so you know it's wired up (microphone capture
  is on the roadmap).
- **RandNet keyboard** (RND-001): the Japan-only RANDnet keyboard.
  Shows the named keys you're holding plus a live typed-text line,
  and lights the Power / Caps / Num Lock LEDs. Full key-matrix decode
  from the n64brew wiki table (untested on the rare real hardware, but
  protocol + scancodes are correct-by-source).
- **Snap Station**: a protocol exerciser for anyone trying to clone
  the Pokémon Snap kiosk hardware.
- **GBA over the link cable**: the ROM auto-multiboots the same
  test payload the GameCube tester uses, then shows you the GBA's
  button state on the same row. No button press required.

After 30 seconds of no input the screen switches to a bouncing
Joypad-logo screensaver. Any button — including a GBA button over
the link cable — wakes it.

## Using it

1. Flash `joypad-tester.z64` to your EverDrive 64 / 64drive /
   SummerCart64 SD card under `roms/`.
2. Boot the N64 and pick `joypad-tester` from your cart menu.
3. The Controller Tester is the landing screen. Every port updates
   in real time as you plug things in or press buttons.
4. **Start + D-pad Down** opens the options menu (so plain Start
   stays a button you can test). From there you can switch into:
   - **Controller Pak Browser**
   - **Game Boy Camera Viewer** (Transfer Pak + GB Camera cart)
   - **Snap Station Test**
   - **About**

In any non-Tester screen, plain **Start** opens the options menu.
**A** confirms, **B** cancels.

### GBA over the link cable

You'll need a GameCube ↔ GBA link cable (DOL-011) plus a small
adapter that splits the GameCube end onto an N64 controller plug.
Joybus is the same protocol on both consoles; only the connector
shells differ.

Wire it up, plug the cable into a powered-on GBA with no cart, and
the tester does the rest — detection, multiboot upload, then a live
GBA-button readout.

## For developers

### Build

```
./build_docker.sh                # Docker-based, no host install needed
./build_docker.sh clean
./build_docker.sh rebuild-image  # bump after libdragon trunk updates
```

The toolchain image layers libdragon trunk on top of
`ghcr.io/dragonminded/libdragon` and runs under `--platform=linux/amd64`
on Apple Silicon hosts (libdragon's upstream image is amd64-only).

CI runs the same Docker build on every push via
[`.github/workflows/verify-build.yml`](../.github/workflows/verify-build.yml).
`build/joypad-tester.z64` is the in-tree output.

### Release flow

Tag format: `n64-v<semver>` (e.g. `n64-v1.0.0`). Per-version notes
live in [`CHANGELOG.md`](CHANGELOG.md); the GitHub release attaches
the `.z64` artifact built by the release workflow.

### Origin / credits

- **Joypad / accessory subsystem**: Christopher Bonhage (meeq)'s
  [JoypadTest-N64](https://github.com/meeq/JoypadTest-N64) (public
  domain), upstreamed into libdragon trunk.
- **Bio Sensor decoder**, **Snap Station**, **GB Camera**, **mempak
  browser**: ports of meeq's `*_Test.c` examples (public domain).
- **GBA multiboot uploader**: port of
  [`../gcn/ppc/gba.c`](../gcn/ppc/gba.c) in this repo, itself ported
  from [Doridian/Joybus-PIO](https://github.com/Doridian/Joybus-PIO)
  + [AxioDL/jbus](https://github.com/AxioDL/jbus). MIT.
- **GBA payload**: built from `../gba/build/tester/tester_mb.gba`
  (this repo's GBA subdir). MIT.
- **libdragon SDK**: public domain, from
  https://github.com/DragonMinded/libdragon.

See [`LICENSE.md`](LICENSE.md) for the full per-file provenance.
