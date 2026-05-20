# Joypad Tester — PC Engine

PC Engine / TurboGrafx-16 build of the [Joypad Tester](../README.md).
Faithful port of dshadoff's `PCE_Mouse_Test` baseline: probes the
joypad port for 2-button and 6-button pads via multitap, plus the PC
Engine mouse.

## What it tests

<p align="center">
  <img src="assets/screenshot.png" alt="PC Engine Joypad Tester main screen: yellow 'Joypad Tester - PC Engine' title, white live P1..P5 + Mouse + abs x/y readouts in two columns, cyan 'Press I button or right-click to toggle mouse mode.' footer" width="600">
</p>

Each of the five potential joypad slots (single pad on port 0, or up to
five through a PCE multitap) is shown live as the raw 16-bit joybus
read. The mouse, if present, gets a separate decoded readout with
per-frame deltas and accumulated absolute X / Y:

```
joy 0: XXXX
joy 1: XXXX
joy 2: XXXX
joy 3: XXXX
joy 4: XXXX

mouse: XX
       x: XX
       y: XX

abs x: XXXX
abs y: XXXX
```

| Source                            | Detected as                                |
|-----------------------------------|--------------------------------------------|
| 2-button pad                      | `joy(N)` with the four standard button bits |
| 6-button pad                      | `joy(N)` with the extended bits exposed when mouse mode is off |
| PC Engine mouse                   | `mouse_exists()` = 1; `mouse_x()` / `mouse_y()` deltas + accumulator |
| Empty slot                        | `joy(N)` = `0xFFFF` (all-high)             |

The **I** button (or right mouse button) toggles between
mouse-decoded mode (calls `mouse_enable()`) and raw-controller mode
(calls `mouse_disable()`). Same toggle as upstream.

Fields a controller doesn't have stay at zero — same convention as
the other consoles in this repo (see [`../gcn/README.md`](../gcn/README.md)
for the prior art).

## HuC toolchain

PC Engine homebrew is built with HuC, a C-to-HuC6280 compiler in active
use since the late 1990s. We pin to [uli/huc](https://github.com/uli/huc)
at commit `52a556a` — a hardened fork with bug fixes, ANSI-style
function declarations, struct/union support, and a 470-case test suite.
The fork hasn't moved since 2018, so the pin is stable.

The pin lives in [`buildtools/Dockerfile`](buildtools/Dockerfile):
changing it means rebuilding the image (`./build_docker.sh
rebuild-image`).

## Build

With HuC installed locally (`huc` on PATH, headers in
`/usr/include/pce/`):

```
make            # → build/joypad-tester.pce
make clean      # nuke build/
```

Most contributors won't have HuC installed; use the Docker image
instead — `build_docker.sh` builds the image once (HuC compiled from
source, ~2 minutes) then reuses it:

```
./build_docker.sh                # build (first run also builds image)
./build_docker.sh clean          # make clean inside the container
./build_docker.sh rebuild-image  # force toolchain image rebuild
```

CI builds on every push to `main` (see
[`.github/workflows/verify-build.yml`](../.github/workflows/verify-build.yml)).

## Loading on hardware

Drop `build/joypad-tester.pce` onto a flash cart that takes raw HuCard
ROMs:

- **EverDrive PCE / EverDrive Pro** — copies straight to the SD card,
  no header conversion needed.
- **Turbo EverDrive (older)** — same.
- **Krikzz / SSDS3 / etc.** — same.

The ROM is a vanilla HuCard image (no 512-byte copier header). On the
Turbografx-16 it'll run through a region adapter or modded console; on
the PC Engine it boots natively.

## Releases

Tagged as `pce-v<semver>` from the repo root — see
[`pce/CHANGELOG.md`](CHANGELOG.md) for per-version notes. The release
workflow attaches `joypad_tester_pce_v<semver>.pce` (the in-tree build
output is `joypad-tester.pce`; the release stages it under a
flash-cart-friendly name).

## Origin / credits

Faithful port of David Shadoff's
[PCE_Mouse_Test](https://github.com/dshadoff/PCE_Mouse_Test) (MIT) —
see [`LICENSE.md`](LICENSE.md). All of the joy / mouse probing logic
comes from upstream; the Joypad Tester subdir contributes the build
infra (pinned-HuC Docker image, `build_docker.sh`, Makefile that emits
the canonical `joypad-tester.pce` artifact name), the per-console
release wiring, and this README. Subsequent iterations (per-port live
button labels, multi-controller-style display matching the GC tester)
will be added on top in follow-up versions; the upstream baseline is
preserved unmodified in `src/joypad_tester.c` for v0.1.0.

HuC compiler from [uli/huc](https://github.com/uli/huc) — an enhanced
fork of David Michel's HuC 3.21, by Ulrich Hecht. Used under HuC's
license terms (see the upstream LICENSE file).
