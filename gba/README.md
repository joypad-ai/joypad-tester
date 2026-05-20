# Joypad Tester — Game Boy Advance

Game Boy Advance build of the [Joypad Tester](../README.md). Two
multiboot ROMs built from one source tree: one ships as the GBA-as-
controller payload that joypad-os multiboots over the GameCube link
cable, the other is a standalone tester ROM you can multiboot from this
repo's GameCube/Wii host or run from a flash cart.

## What it tests

<p align="center">
  <img src="assets/screenshot.png" alt="GBA Joypad Tester tester-variant rendering: 'JOYPAD TESTER' header, 'GC Link: connected', 2-column live button list (A/B/Sel/Start/L on the left, Up/Down/Left/Right/R on the right), and a Raw hex of the joybus payload" width="480">
</p>

Each variant boots the same Doridian-style joybus controller loop and
reports button state back to the host the moment a button changes. The
two variants differ in what they render on the GBA screen:

```
joypad_mb.gba   eyes overlay (cartoon eyes + emotion state machine)
tester_mb.gba   on-GBA text console + idle screensaver
```

| Variant      | On-GBA display                                                                                   | Intended host                 |
|--------------|--------------------------------------------------------------------------------------------------|-------------------------------|
| `joypad_mb`  | Mode-4 framebuffer + cylinder-eye renderer + emotion state machine (port of joypad-os's `eyes_anim`) | [joypad-os](https://github.com/joypad-ai/joypad-os) (RP2040) |
| `tester_mb`  | 30×20 text console (live button state, 2-column layout) + idle screensaver (Mode-4 page-flipped) | This repo's GameCube/Wii host |

Fields the GBA doesn't have (analog sticks, triggers, C-stick, rumble)
don't exist on the wire — only the 10 face/dpad/shoulder/start/select
buttons are reported. The 2-byte joybus payload carries them.

## Joybus protocol

The handshake + main loop is taken verbatim from
[Doridian/Joybus-PIO](https://github.com/Doridian/Joybus-PIO)'s GBA
side — `0x30303030` exchange, status-bit polls, SVC `0x26` BIOS reset
on `JOYCNT.RST`. Modifying it breaks the cable's level-shifter MCU, so
both variants share the same loop and only swap the on-screen renderer
behind it.

Hot-swap and host-reboot re-multiboot rely on noticing a host `cmd 0xFF`
within a frame: `REG_JOYCNT.RST` is polled inside the VBlank busy-wait
so any reset triggers `SystemCall(0x26)` within microseconds.

## Eyes variant (`joypad_mb.gba`)

Renders a pair of cartoon eyes (Mode-4 8bpp paletted framebuffer) whose
gaze follows the dpad like a self-centering analog stick. An emotion
state machine cycles `ACTIVE → wander → sleep` with per-emotion FG /
pupil palette swaps. Designed to ship inside joypad-os as the GBA-as-
controller "personality" layer — the buttons remain the product, the
overlay just confirms the link is alive.

## Tester variant (`tester_mb.gba`)

A 30-column × 20-row text console:

```
Joypad Tester — GBA
GC Link: connected

Buttons:
  A:0          Start:0
  B:0          Select:0
  L:0          Up:0
  R:0          Down:0
                Left:0
                Right:0

Raw: 0000
```

Live indicators flip 0/1 as buttons change; `Raw: XXXX` is the hex
joybus payload. After 30 seconds of no input, a Mode-4 page-flipped
screensaver kicks in: the 64×54 joypad logo bounces off the screen
edges, color-cycling through red / green / yellow / blue / magenta /
cyan / white on every wall hit — same image, palette, and speed as the
GameCube tester's idle screensaver, so testers see the same animation
on both screens.

If the host drops the joybus link, the variant falls back to standalone
mode and the console keeps reporting button state directly from
`REG_KEYINPUT` — useful as a pure flash-cart button tester.

## Build

With devkitPro / devkitARM installed:

```
make            # → build/joypad/joypad_mb.gba + build/tester/tester_mb.gba
make joypad     # → build/joypad/joypad_mb.gba  only
make tester     # → build/tester/tester_mb.gba  only
make clean      # nuke build/
```

Without devkitPro, use the Docker image:

```
docker run --rm -v "$PWD":/workspace -w /workspace \
  devkitpro/devkitarm:latest make
```

CI builds both variants on every push to `main` (see
[`.github/workflows/verify-build.yml`](../.github/workflows/verify-build.yml)).

The `build/` tree is committed so consumers using this folder as a
submodule (e.g. joypad-os) don't need devkitARM unless they're
modifying the source — rebuild and commit `build/<variant>/<variant>_mb.gba`
+ `build/<variant>/<variant>_payload.c` whenever you change source.

## Loading on hardware

Two paths, depending on which variant and host:

**Multiboot over GameCube link cable** — both variants. Build this
repo's GameCube/Wii host (see [`../gcn/`](../gcn/)), connect a
GBA Link Cable from GameCube SI port 2 to the GBA, leave the GBA on the
"Press Start" screen with no cartridge, and start the host. The host
uploads `tester_mb.gba` via Kawasedo handshake + stream cipher and the
GBA boots into the tester variant. For the eyes variant, the host
uploader lives in [joypad-os](https://github.com/joypad-ai/joypad-os)'s
RP2040 firmware (`src/native/host/gc/gba_multiboot.c`).

**Flash cart** — `tester_mb.gba` only. Drop `tester_mb.gba` onto an
EZ-Flash / EverDrive / etc., boot it. Standalone fallback kicks in
after the 3-second joybus handshake timeout and the variant runs as a
pure on-GBA button tester.

## Embedding

Drop the appropriate `build/<variant>/<variant>_payload.c` into your
host firmware's source list, then feed the symbols to your joybus
multiboot uploader. Both variants export the same symbol names so the
host's externs don't change when switching ROMs:

```c
extern const uint8_t  joypad_payload[];
extern const uint32_t joypad_payload_len;
```

See [`../gcn/ppc/gba.c`](../gcn/ppc/gba.c) or joypad-os's
`src/native/host/gc/gba_multiboot.c` for a reference uploader
implementation (Kawasedo handshake, stream cipher, polled WRITE/READ,
unconditional handshake-complete write).

## Releases

Tagged as `gba-v<semver>` from the repo root — see
[`gba/CHANGELOG.md`](CHANGELOG.md) for per-version notes. The release
workflow attaches `joypad_tester_gba_v<semver>.gba` (the standalone button
tester, renamed at staging time for flash-cart drop-in legibility).
The eyes variant (`joypad_mb.gba`) is a firmware payload meant to be
multibooted by a host firmware (joypad-os keeps its own copy for that
role); it builds in `gba/build/joypad/` for source-tree inspection but
doesn't get a Release attachment.

## Origin / credits

Built on Doridian's [Joybus-PIO](https://github.com/Doridian/Joybus-PIO)
GBA payload (MIT) — see [`LICENSE.md`](LICENSE.md). The joybus handshake
+ main loop (`0x30303030` exchange, status-bit polls, SVC `0x26` BIOS
reset on `JOYCNT.RST`) is taken verbatim from Doridian's `gba/source/main.c`;
the eyes overlay, tester console, Mode-4 page-flipped screensaver
(matching the GameCube tester's logo + color cycle), two-build Makefile,
and robust joybus reset handling (polling `JOYCNT.RST` inside the VBlank
busy-wait) are added on top. Joyboot stream cipher reverse-engineered by
Sage-of-Mirrors' [libgbacom](https://github.com/Sage-of-Mirrors/libgbacom)
(ported from VisualBoyAdvance); canonical GameCube-side multiboot
reference by FIX94's
[gc-gba-link-cable-demo](https://github.com/FIX94/gc-gba-link-cable-demo);
hardware semantics per [gbatek](https://problemkaputt.de/gbatek.htm)
(Martin Korth). Eyes overlay is a port of joypad-os's `eyes_anim`.
