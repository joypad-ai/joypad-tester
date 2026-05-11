# joypad-tester / gba

GBA multiboot payloads that ship inside [joypad-tester](..). Two ROMs
build from this one source tree:

- **`joypad_mb.gba`** — Doridian-style joybus controller + animated
  cartoon eyes overlay. Consumed by
  [joypad-os](https://github.com/joypad-ai/joypad-os) via submodule for
  its GBA-as-controller mode.
- **`tester_mb.gba`** — Doridian-style joybus controller + an on-GBA
  console showing live button state, with an idle screensaver that
  bounces the joypad logo (color-cycling on each wall hit) à la the
  GameCube tester. Embedded into joypad-tester's GameCube/Wii host so
  testers see visual feedback on the GBA, and runnable standalone from
  a flash cart as a pure button-tester ROM.

The `build/` tree ships pre-built artifacts so consumers using this
folder as a submodule (e.g. joypad-os) don't need devkitARM unless
they're modifying the source:

```
build/joypad/joypad_mb.gba       # eyes ROM (raw multiboot binary)
build/joypad/joypad_payload.c    #          (C array for embedding)
build/tester/tester_mb.gba       # tester ROM (raw multiboot binary)
build/tester/tester_payload.c    #            (C array for embedding)
```

Each `*_payload.c` exports the same symbol names:

```c
extern const uint8_t  joypad_payload[];
extern const uint32_t joypad_payload_len;
```

so a host can drop either file in and the embedding code doesn't have
to change. Pick the variant that fits your product.

## Lineage

The joybus handshake + main loop in `source/main.c` and
`source/main_tester.c` (the `0x30303030` exchange, status-bit polls,
SVC `0x26` BIOS reset on `JOYCNT.RST`) is taken from **Doridian's
[Joybus-PIO](https://github.com/Doridian/Joybus-PIO)** `gba/source/main.c`
(2023). That sequence is load-bearing for the cable's level-shifter MCU;
modifying it breaks the host's view of input. The eyes overlay and the
tester console run on top of it.

Chain of credit:

- **[gbatek](https://problemkaputt.de/gbatek.htm)** (Martin Korth) —
  authoritative GBA hardware reference (SIO / Joybus / BIOS reset
  semantics)
- **[libgbacom](https://github.com/Sage-of-Mirrors/libgbacom)**
  (Sage-of-Mirrors, ported from VisualBoyAdvance) — original
  reverse-engineering of the joyboot stream cipher
- **[gc-gba-link-cable-demo](https://github.com/FIX94/gc-gba-link-cable-demo)**
  (FIX94) — canonical GameCube-side reference for booting a GBA over the
  link cable
- **[Joybus-PIO](https://github.com/Doridian/Joybus-PIO)** (Doridian) —
  RP2040-PIO implementation of the GameCube side, plus the GBA-side
  controller payload these variants build on
- **This subtree** — eyes overlay (port of joypad-os's `eyes_anim`),
  Mode-4 page-flipped screensaver matching the joypad-tester GC
  variant's logo + colour cycle, two-build Makefile, robust joybus
  reset handling (polls `JOYCNT.RST` inside the VBlank busy-wait so
  host cmd 0xFF triggers `SystemCall(0x26)` within microseconds)

## Embedding

Drop the appropriate `build/<variant>/<variant>_payload.c` into your
host firmware's source list, then feed `joypad_payload[]` /
`joypad_payload_len` to your joybus multiboot uploader. See
[joypad-os](https://github.com/joypad-ai/joypad-os)'s
`src/native/host/gc/gba_multiboot.c` or this repo's
[`../gamecube/ppc/gba.c`](../gamecube/ppc/gba.c) for reference uploader
implementations (Kawasedo handshake, stream cipher, polled WRITE/READ,
unconditional handshake-complete write).

## Building (only if you change source)

Requires devkitPro / devkitARM:

```bash
# macOS
brew install --cask devkitpro-pacman
sudo dkp-pacman -S gba-dev

# Linux
# https://devkitpro.org/wiki/devkitPro_pacman

export DEVKITPRO=/opt/devkitpro

make            # build both variants
make joypad     # build only the eyes ROM
make tester     # build only the tester ROM
make clean      # nuke build/
```

Outputs land in `build/joypad/` and `build/tester/`. Both `*_mb.gba`
and `*_payload.c` are committed; rebuild and commit them whenever you
change source.

## Layout

```
.
├── Makefile                       # two-variant build → build/<variant>/
├── source/
│   ├── main.c                     # eyes variant entry (joybus + eyes_anim)
│   ├── main_tester.c              # tester variant entry (joybus + console
│   │                              # + Mode-4 page-flipped screensaver)
│   ├── display.c/.h               # Mode-4 framebuffer for the eyes variant
│   ├── eyes_anim.c/.h             # cylinder-eye renderer + emotion state
│   │                              # machine
│   └── platform/
│       └── platform.h             # stub for shared eyes_anim platform API
├── tools/
│   └── bin2c.py                   # binary .gba → C array converter
└── build/                         # committed pre-built payload artifacts
    ├── joypad/
    │   ├── joypad_mb.gba
    │   └── joypad_payload.c
    └── tester/
        ├── tester_mb.gba
        └── tester_payload.c
```
