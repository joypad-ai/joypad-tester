# Joypad Tester — NUON

NUON-enhanced DVD player (Samsung DVD-N501 / N504 / N505 / N2000,
Toshiba SD2300, RCA DRC300N / DRC480N) build of the
[Joypad Tester](../README.md). Reads the BIOS `_Controller[]`
array and renders the live state of all four joypad ports plus the
IR remote on screen.

## What it tests

The full publicly-documented NUON controller state for ports 1-4
(`_Controller[0..3]`) plus the IR remote bitfield. On-screen layout:

```
                 Joypad Tester - NUON

P1 ON  A B L R St Z  U D L R  cU cD cL cR  X +47 Y -12  P:00100000  raw 00 00 00 00
P2 --  (no controller)
P3 --  (no controller)
P4 --  (no controller)

IR remote  00000000-00000000-00000000-00000000 (0x00000000)
```

Per-port fields:

| Field | Meaning |
|-------|---------|
| `A B L R St Z` | Face buttons + shoulders + Start + NUON (Z) button. Yellow = held. |
| `U D L R` | D-pad. |
| `cU cD cL cR` | C-pad cluster (the four directional triggers under the right thumb). |
| `X +nn Y +nn` | Signed 8-bit analog X / Y axes (NUON spec range -128..127). |
| `P:nnnnnnnn` | Raw 22-bit `properties` word — identifies the controller family (standard pad vs. wheel / lightgun / fishing rod / mouse / etc.) so users can confirm what the BIOS thinks is plugged in. |
| `raw nn nn nn nn` | Scalar-2/3 tail (`d3.xAxis2`, `d4.yAxis2`, `d5.spinner1`, `d6.spinner2`). For non-standard peripherals these bytes carry secondary axes, throttle/brake, mouse X/Y deltas, spinner values, etc. Printing the raw bytes lets users explore unknown devices without us baking in a per-type decoder. |
| `IR remote` | Full 32-bit `_Controller[0].remote_buttons`. The mapping isn't uniform across Samsung / RCA / Toshiba units, so we just show every bit and let the user press each remote button in turn to map them. |

| Source | Detected as |
|--------|-------------|
| Standard NUON / Logitech Adrenaline pad | `_Controller[0..3]` with `status=1`, populated buttons + analog X/Y |
| NUON mouse / wheel / fishing rod | same slot, distinguish via `properties` word + the raw tail bytes |
| IR remote on the player itself | `_Controller[0].remote_buttons` (other slots have this field zero) |

The four-port maximum matches the NUON spec; retail players ship
with two physical ports, but the daisy-chain protocol allows up to
four controllers on a multitap.

## Controller polling

Slot 0 is auto-polled every frame by the BIOS — no action needed.
Slots 1-3 must be explicitly probed each frame with `_DeviceDetect(i)`
or hot-plug events are silently dropped. `src/main.c`'s main loop
does this unconditionally; if you fork the tester to add a feature,
keep the per-frame `_DeviceDetect()` calls.

The `ControllerData` struct in `nuon/joystick.h` is `volatile` —
the BIOS writes through it from interrupt context, so the compiler
must re-read the fields each iteration. The `ButtonA()`/`ButtonB()`
macros expand to `c.buttons & CTRLR_BUTTON_X`, which the compiler
treats as a fresh load every time.

## Signed-DVD path (2022 keys)

NUON was designed to refuse unsigned code from retail DVD media —
each `nuon.run` payload has a GPG signature blob prepended by
`vmmakeapp`, verified by the player's BIOS at boot. The
authentication keys were rediscovered by AltRN8 and ported to
modern hosts by EdgeConnector and mgarcia in 2022, and the working
`bless/` framework now ships in
[cubanismo/nuon-sdk](https://github.com/cubanismo/nuon-sdk).

The build pipeline (`Makefile`) chains the post-compile steps:

```
hello.cof → vmstrip -F → .stripped.cof
          → coffpack    → .packed.cof
          → vmmakeapp 0 → .packed.cof.app
          → mv          → nuon.run        (signed, ready for /NUON/)
```

`vmmakeapp <input> 0` signs with key type 0 = "Application" — the
type retail games are signed with. The trailing `0` matters; without
it the signature is for a driver / library context the BIOS bootstrap
won't accept.

## Build

The toolchain is Docker-only on macOS / Windows (the SDK Linux
binaries are 32-bit x86 ELFs, running under `--platform=linux/386`).
Linux users with `~/git/nuon-sdk` checked out can build natively:

```sh
./build_docker.sh                # Docker-based, no native install needed
./build_docker.sh clean
./build_docker.sh rebuild-image

# Or, with cubanismo/nuon-sdk's env.sh already sourced:
make                             # produces build/joypad-tester.iso
```

`build/joypad-tester.iso` is the in-tree output. CI builds on every
push to `main` (see
[`.github/workflows/verify-build.yml`](../.github/workflows/verify-build.yml)).

`build_docker.sh` prefers `$HOME/git/nuon-sdk` (a developer fork
checkout) and falls back to a shallow clone of
[`cubanismo/nuon-sdk`](https://github.com/cubanismo/nuon-sdk) into
`nuon/.nuon-sdk/`.

## Loading on hardware

### Desktop emulator (NuanceResurrection)

Drop `build/joypad-tester.cof` onto
[andkrau/NuanceResurrection](https://github.com/andkrau/NuanceResurrection)
(0.6.7 or later) and load via File → Open. Best path for iterating
on the source. Bypasses the signing / ISO mastering steps entirely.

### Real NUON-enhanced DVD player

Burn `joypad_tester_v<ver>_nuon.iso` to a DVD-R (single-layer, NOT
DVD-RW / DVD+R / DVD+RW / DVD-RAM — the BIOS is picky and only
DVD-R is universally accepted). Boot the disc in your player:
the tester loads automatically as the `/NUON/nuon.run` payload.

The release also ships `joypad_tester_v<ver>_nuon_toshiba.iso` —
identical content, but mastered on macOS with `hdiutil -format
UNIV` because the Toshiba SD2300 BIOS rejects ISO9660+UDF images
made by every Linux / Windows tool. If you have an SD2300, use
the `_toshiba` variant. Other players accept either.

### Bare nuon.run (for multi-app discs)

`joypad_tester_v<ver>_nuon.run` is the signed payload only. Drop
it into your own DVD layout (e.g. as one entry inside Skah_T's
[NUON Boot Loader](http://www.dragonshadow.com/-/bootload/)
applist.txt) to bundle the tester with other homebrew on a single
disc.

## Releases

Tagged as `nuon-v<semver>` from the repo root — see
[`nuon/CHANGELOG.md`](CHANGELOG.md) for per-version notes. The
release workflow attaches three artifacts:

- `joypad_tester_v<ver>_nuon.iso` — UDF ISO from Linux `genisoimage`. Boots on Samsung, RCA, GoldStar, every player except Toshiba SD2300.
- `joypad_tester_v<ver>_nuon_toshiba.iso` — `hdiutil` Universal format, the only known way to boot on Toshiba SD2300.
- `joypad_tester_v<ver>_nuon.run` — signed payload for users composing their own multi-app DVD.

## Origin / credits

Built against [cubanismo/nuon-sdk](https://github.com/cubanismo/nuon-sdk) —
James Jones's modern repackaging of the leaked VM Labs NUON SDK
(Internal Release 0.86.2, June 2001) plus the DVD authentication
framework rediscovered after the [2022 signing-key release](https://www.resetera.com/threads/authentication-keys-used-for-signing-games-for-the-nuon-gaming-platform-now-discovered-all-nuon-owners-can-now-self-sign-apps.592107/)
by AltRN8 and ported to modern hosts by EdgeConnector and mgarcia.

The Joypad Tester source (`src/main.c`) is original — written
from scratch against the public BIOS joystick API
(`_Controller[]`, `_DeviceDetect()`, `ButtonA()` etc.) documented
in `nuon/joystick.h`. The VM Labs Game-Controllers sample served
as a reference for the API surface, but no sample source is copied.

The original VM Labs SDK headers carry a "Confidential and
Proprietary" notice and were never formally open-sourced; the
NUON homebrew scene ([NUON-Dome](https://www.nuon-dome.com),
Skah_T's Boot Loader, the Nuance/NuonResurrection emulator, etc.)
has operated on abandonware footing for 20+ years. We consume
these headers via the toolchain at build time only — none ship
in this repository. See [`LICENSE.md`](LICENSE.md) for the full
provenance breakdown.
