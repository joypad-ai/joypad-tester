# joypad-gba

GBA multiboot payload that doubles as a USB controller (via the GameCube↔GBA
link cable) and renders animated cartoon eyes on the GBA screen.

Designed to be embedded into firmware that drives the GameCube side of a
joybus link — e.g. RP2040-based controller adapters — but the payload itself
is host-agnostic. Anyone with a working joybus multiboot uploader can use it.

The `build/` directory ships pre-built artifacts so consumers using this
repo as a submodule don't need devkitARM unless they're modifying the source:

- **`build/joypad_payload.c`** — the payload as a C array (`joypad_payload[]`
  + `joypad_payload_len`) ready to compile into a host project
- **`build/joypad_mb.gba`** — the raw multiboot binary, suitable for dumping
  onto a flashcart or piping through any joyboot uploader

## Lineage

The joybus handshake + main loop in `source/main.c` (the `0x30303030`
exchange, `ResetHalt`, `REG_JSTAT` polls, SVC `0x26` BIOS reset on
`JOYCNT.RST`) is taken verbatim from **Doridian's
[Joybus-PIO](https://github.com/Doridian/Joybus-PIO)** `gba/source/main.c`
(2023). That sequence is load-bearing for the cable's level-shifter MCU;
modifying it breaks the host's view of input. The animated-eyes overlay
runs in the VBlank slot on top of it.

Chain of credit:

- **[gbatek](https://problemkaputt.de/gbatek.htm)** (Martin Korth) —
  authoritative GBA hardware reference (SIO / Joybus / BIOS reset semantics)
- **[libgbacom](https://github.com/Sage-of-Mirrors/libgbacom)** (Sage-of-Mirrors,
  ported from VisualBoyAdvance) — original reverse-engineering of the
  joyboot stream cipher
- **[gc-gba-link-cable-demo](https://github.com/FIX94/gc-gba-link-cable-demo)**
  (FIX94) — canonical GameCube-side reference for booting a GBA over the
  link cable
- **[Joybus-PIO](https://github.com/Doridian/Joybus-PIO)** (Doridian) —
  RP2040-PIO implementation of the GameCube side, plus the GBA-side
  controller payload this repo forks
- **This repo** — animated-eyes overlay (port of joypad-os's `eyes_anim`),
  per-emotion FG/pupil palette, dpad-as-self-centering-analog gaze,
  ACTIVE → wander → sleep cycle

## Embedding

Drop `build/joypad_payload.c` into your host firmware's source list. It
exports:

```c
extern const uint8_t  joypad_payload[];
extern const uint32_t joypad_payload_len;
```

Then feed those bytes to your joybus multiboot uploader. See
[joypad-os](https://github.com/joypad-ai/joypad-os)'s
`src/native/host/gc/gba_multiboot.c` for a reference uploader implementation
(Kawasedo handshake, stream cipher, polled WRITE/READ).

## Building (only if you change source)

Requires devkitPro / devkitARM:

```bash
# macOS
brew install --cask devkitpro-pacman
sudo dkp-pacman -S gba-dev

# Linux
# https://devkitpro.org/wiki/devkitPro_pacman

export DEVKITPRO=/opt/devkitpro
make
```

Outputs:

```
build/joypad_mb.gba       # multiboot ROM
build/joypad_payload.c    # same bytes as a C array
```

Both are committed in this repo; rebuild and commit them whenever you change
the source.

## Layout

```
.
├── Makefile             # devkitARM build → build/joypad_mb.gba + .c
├── source/
│   ├── main.c           # entry point, IRQ wiring, joybus handshake (Doridian)
│   ├── display.c/.h     # Mode-4 framebuffer + DMA present
│   ├── eyes_anim.c/.h   # cylinder-eye renderer + emotion state machine
│   └── platform/
│       └── platform.h   # stub for shared eyes_anim platform API
├── tools/
│   └── bin2c.py         # binary .gba → C array converter
└── build/               # committed pre-built payload artifacts
    ├── joypad_mb.gba
    └── joypad_payload.c
```

The flat layout (no `gba/` subdir like Doridian's repo, since this whole
repo *is* the GBA payload) keeps embedding paths short.
