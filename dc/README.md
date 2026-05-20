# Joypad Tester — Dreamcast

Sega Dreamcast build of the [Joypad Tester](../README.md). Reads every
maple-bus device the console can talk to — controllers, mice, keyboards,
light guns, plus per-port slot peripherals (VMU, Purupuru rumble pack,
microphone) — and renders the live state across all four ports. Also
hosts a built-in VMU icon editor mode (unlocked in v0.2) for creating,
extracting, and applying `ICONDATA_VMS` saves on real hardware.

## What it tests

<p align="center">
  <em>screenshot.png — placeholder, captured once v0.1 builds on hardware</em>
</p>

All four maple ports (A/B/C/D) rendered live, simultaneously, with no
active-port toggle. Each port shows controller type plus both expansion
slots probed for peripherals:

```
Port A  Style: Pad      Slot1: VMU(192/200)   Slot2: Purupuru
Stick: +000,+000  Trig: L000 R000
A:0 B:0 X:0 Y:0 Start:0  D-U:0 D-D:0 D-L:0 D-R:0

Port B  Style: Mouse    Slot1: ---            Slot2: ---
Pos: +0000,+0000  Wheel: +0  L:0 R:0 M:0

Port C  Style: Keyboard Slot1: ---            Slot2: ---
Keys held: A SHIFT      Scancodes: 04 E1

Port D  Style: Empty    Slot1: ---            Slot2: ---
```

| Style    | Source                                                |
|----------|-------------------------------------------------------|
| Pad      | Standard DC controller (`MAPLE_FUNC_CONTROLLER`)      |
| Mouse    | DC mouse (`MAPLE_FUNC_MOUSE`)                         |
| Keyboard | DC keyboard (`MAPLE_FUNC_KEYBOARD`)                   |
| LightGun | DC light gun (`MAPLE_FUNC_LIGHTGUN`)                  |
| Empty    | No device on this port                                |

| Slot device | Function mask                                       |
|-------------|------------------------------------------------------|
| VMU         | `MAPLE_FUNC_MEMCARD` (+ `_LCD` + `_CLOCK` on real VMU) |
| Purupuru    | `MAPLE_FUNC_PURUPURU` (rumble pack)                   |
| Microphone  | `MAPLE_FUNC_MICROPHONE`                                |
| ---         | Empty slot                                            |

Fields a controller doesn't have (e.g. analog triggers on a keyboard)
stay at zero — same convention as
[`gcn/README.md`](../gcn/README.md) and the libdragon reference.

## Maple-bus peripheral probing

When a VMU is detected, the port row shows `VMU(<free>/<total>)` — block
count read once per second via `MAPLE_FUNC_MEMCARD`. The same poll
verifies the LCD (`MAPLE_FUNC_LCD`) and clock (`MAPLE_FUNC_CLOCK`)
subdevices respond, which separates a "real" VMU from a memcard-only
pack.

### Rumble actuation

Hold **A** on any port whose slot has a Purupuru pack to actuate the
motor (`purupuru_rumble_raw(addr, 0x10F419F0)`, a moderate strength
short pulse pattern). The slot label flips `Purupuru` → `Purupuru*`
while held. Mirrors the gcn/ Rumble Pak A-hold convention.

### VMU LCD test

Hold **B** on any port with a VMU to push the Joypad Tester logo
(48×32 1bpp bitmap) to that VMU's LCD via `vmu_draw_lcd_xbm()`. Verifies
the LCD subdevice independently of the memcard subdevice — handy when
diagnosing third-party packs that fake one capability mask but not the
other.

### VMU clock

Hold **Y** on a port with a VMU to read the real-time clock subdevice
(`MAPLE_FUNC_CLOCK`) and display `RTC: YYYY-MM-DD HH:MM:SS` alongside
the slot label. Most third-party memcards stub this out — the clock
reading is a quick authenticity check.

## Options menu

Hold **Start + Down** at any time to open a modal overlay listing the
modes:

```
┌─ OPTIONS ──────────────────┐
│  > Controller Tester       │
│    VMU Icon Editor         │
│    About                   │
│                            │
│  Video: VGA 640×480 60Hz   │
└────────────────────────────┘
```

D-pad navigates, **A** confirms, **Start** closes. Mouse and keyboard
work too — click an entry, or arrow + Enter. The menu renders over a
dimmed copy of whatever mode is active so the player knows it's a modal,
not a navigation.

The Controller Tester is the default landing mode (matches every other
console in this repo). As of v0.2.1, the menu offers five modes:

- **Controller Tester** — the per-port live readout described above.
- **VMU Icon Editor** — two side-by-side canvases (color + mono,
  192×192 each at 6× zoom) matching the web `dreamcast-icon-maker`
  layout. Cursor-region-sensitive: A paints in whichever pane the
  cursor is over, B erases. **Mono palette toggle row** (one cell
  per color-palette index) flips every mono pixel of that color
  for instant color→silhouette translation. Per-canvas Reset
  buttons, mono Invert button, Real Mode toggle, Apply (writes
  `ICONDATA_VMS` with backup-on-replace), Save (appends to
  library), Name (opens on-screen keyboard for the description
  field — real maple keyboards also feed into the same buffer).
- **VMU Save Browser** — enumerates every save on every detected
  VMU with thumbnail decode. A loads an icon into the editor;
  Y applies it directly to its source VMU's `ICONDATA_VMS`; X
  refreshes the list. Read-only on game saves.
- **Library Browser** — read-back UI for `VMUICONS.VMS` library
  saves. Lists every entry across every VMU with thumbnail and
  flag indicators (`*` = auto-backup, `R` = Real Mode set). A
  loads to editor; B prompts to delete; X refreshes.
- **About** — version, detected cable, region, mode.

## VGA detection

KOS `vid_check_cable()` is read once at boot and the appropriate
video mode is set:

| Cable                | Mode                          |
|----------------------|-------------------------------|
| `CT_VGA`             | `DM_640x480_VGA`              |
| `CT_RGB` (PAL/NTSC)  | `DM_640x480_*_IL` per region  |
| `CT_COMPOSITE`       | `DM_640x480_NTSC_IL` (NTSC consoles) / `_PAL_IL` (PAL) |

VGA is the recommended setup for fine pixel-art editing in the VMU
Icon Editor mode — 480i causes inter-field flicker on single-pixel
edges. The tester itself is readable on any cable; the editor (v0.2)
will auto-tune zoom factor and minimum line width when running
interlaced. IP.BIN is marked VGA-compatible so VGA boxes don't reject
the disc.

## KallistiOS toolchain

DC homebrew is built with [KallistiOS](https://github.com/KallistiOS/KallistiOS)
(KOS) plus its bundled `dc-chain` sh4-elf cross-compiler. We pin a
specific KOS commit inside [`buildtools/Dockerfile`](buildtools/Dockerfile);
bumping it means rebuilding the image (`./build_docker.sh
rebuild-image`).

The toolchain is Docker-only on macOS / Apple Silicon. On Linux x86_64
hosts you can also install KOS natively and run `make` from this
directory after sourcing `$KOS_BASE/environ.sh`; the Docker path is
the supported route for CI parity.

## Build

```
./build_docker.sh                # build (first run also builds image, ~10 min)
./build_docker.sh clean          # nuke build/
./build_docker.sh rebuild-image  # force toolchain image rebuild
```

In-tree output: `build/joypad-tester-dreamcast.cdi` (selfboot disc
image). The intermediate `.elf` lives in `build/` for emulator
debugging. CI builds on every push to `main` (see
[`.github/workflows/verify-build.yml`](../.github/workflows/verify-build.yml)).

## Loading on hardware

### Emulator (Flycast / Redream)

Drop `joypad_tester_dc_v<ver>.cdi` onto the emulator. Flycast emulates
all maple-bus device classes (controller, mouse, keyboard, light gun,
VMU, Purupuru) and is the most faithful target for testing peripheral
detection logic.

### Real Dreamcast (GDEMU / MODE / SD-loader)

Copy the `.cdi` to the loader's SD card and select it from the menu.
GDEMU's firmware handles `.cdi` natively; MODE and similar ODE devices
do too.

### Burned CD-R

Burn the `.cdi` with DiscJuggler (Windows) or `cdi2nero` / `mkpsxiso`
forks (Linux/macOS) to a 700 MB CD-R. Boots on any unmodified retail
Dreamcast — the disc is a selfboot image with a valid IP.BIN.

## Releases

Tagged as `dc-v<semver>` from the repo root — see
[`dc/CHANGELOG.md`](CHANGELOG.md) for per-version notes. The release
workflow attaches `joypad_tester_dc_v<semver>.cdi` to each GitHub
Release.

## Origin / credits

The Joypad Tester source under `dc/src/` is original. The mode-switching
scaffold (default tester mode + options menu + secondary modes) is a
pattern intended to extend across the other consoles in this repo over
time — see [top-level CLAUDE.md](../CLAUDE.md) for the convention.

The VMU Icon Editor mode (v0.2+) ports byte-level ICONDATA_VMS encode
/decode logic from
[RobertDaleSmith/vmu-icon-maker](https://github.com/RobertDaleSmith/vmu-icon-maker)
(MIT). Background reading on the format:

- [Dreamcast Programming — ICONDATA_VMS](https://nz17.com/interactive/dreamcast/dv_icy-dreamcast_vmu_icon_viewer_and_converter/Dreamcast%20Programming%20-%20ICONDATA_VMS.txt)
- [Elysian Shadows EVMU `icondata.h`](https://vmu.elysianshadows.com/evmu__icondata_8h.html) — Real Mode 3D menu reference.

KallistiOS used unmodified under its BSD-style license. See
[`LICENSE.md`](LICENSE.md).
