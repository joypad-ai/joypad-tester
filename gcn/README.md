# Joypad Tester — GameCube

GameCube/Wii build of the [Joypad Tester](../README.md). Tests every kind
of controller the SI bus can speak to — native GameCube controllers, N64
controllers (via passive N64-to-GC adapter), N64 mice, and the GameCube ASCII
keyboard.

## What it tests

<p align="center">
  <img src="assets/screenshot.png" alt="GameCube Joypad Tester running in Dolphin showing GCN on Port 1, GBA-multiboot on Port 2, ASCII keyboard on Port 3, and N64 controller with Rumble Pak on Port 4" width="600">
</p>

All four SI ports rendered live, simultaneously, with no active-port toggle.
Each port shows:

```
Port N  Style: GCN  Pak: None         Rumble: Idle
Stick: +000,+000 C-Stick: +000,+000 L-Trig:000 R-Trig:000
A:0 B:0 X:0 Y:0 L:0 R:0 Z:0 Start:0
D-U:0 D-D:0 D-L:0 D-R:0 C-U:0 C-D:0 C-L:0 C-R:0
```

| Style    | Source                                     |
|----------|--------------------------------------------|
| GCN      | Standard GameCube controller / WaveBird    |
| N64      | N64 controller via passive adapter         |
| Mouse    | N64 mouse                                  |
| Keyboard | GameCube ASCII keyboard (rare, JP-region)  |
| None     | Empty port                                 |

Fields a controller doesn't have (e.g. C-stick on N64, X/Y face buttons)
stay at zero — same convention as libdragon's
[JoypadTest-N64](https://github.com/meeq/JoypadTest-N64) reference.

## N64 accessory paks

When an N64 controller is detected the app probes the expansion slot every
~250ms and shows the result in the `Pak:` field:

| Probe value (write/read at 0x8000) | Pak              |
|-----------------------------------|-------------------|
| label area writes/reads round-trip | Memory Pak       |
| `0x80`                            | Rumble Pak        |
| `0x81`                            | Bio Sensor        |
| `0x84`                            | Transfer Pak (powered off after probe) |
| `0x85`                            | Snap Station      |

The probe sequence is borrowed from libdragon's
`joypad_accessory_detect_async`. Hot-swapping the pak module without
unplugging the controller relies on a controller reset (cmd `0xFF`) issued
when the cached pak is `None` — without that, N64 controllers don't refresh
their expansion-slot detection signal.

### Rumble actuation

Hold **A** on any port whose controller supports rumble:

- **GCN controller** → built-in motor via `PAD_ControlMotor`
- **N64 + Rumble Pak** → 32 bytes of `0x01` written to `0xC000`

`Rumble:` cycles `Idle` → `Active` → `Idle` accordingly.

### Bio Sensor heart rate

When a Bio Sensor pak is detected the `Rumble:` slot is replaced with
`BPM: NNN (Pulsing)` / `(Resting)`:

- The sensor's pulse register at `0xC000` returns `0x00` while a heartbeat
  is in progress, `0x03` while resting between beats. We count
  `pulsing → resting` transitions per 500ms window and average over a
  rolling 8–16 window buffer to compute BPM.
- BPM stays at `000` for the first ~4–5 seconds while the window fills,
  then settles into a real reading provided you keep your finger on the
  sensor.
- Touch sensitivity varies — light contact may not register as
  `Pulsing` at all.

## GameCube ASCII keyboard

Detection: `SI_GC_KEYBOARD` (0x08200000). Polling: a 3-byte command
`[0x54, 0x03, 0x00]` (cmd, mode, rumble flag) — sending only the cmd byte
returns stale state. Response: 8 bytes — `counter:4 + status:4`, three
reserved bytes, three `keypress` slots, then a checksum byte.

The display replaces the per-port rows with `Keys held:` showing up to
three simultaneous key labels (3-key rollover is the protocol's hard
limit), plus a `Scancodes:` row with the raw bytes and the running
counter for diagnosis. Scancode mapping mirrors the table in
[joypad-os/src/lib/joybus-pio](https://github.com/joypad-ai/joypad-os/blob/main/src/lib/joybus-pio/include/gamecube_definitions.h)
(reverse-engineered from PSO Episode I & II).

## Build

With devkitPro installed:

```
TARGET_CONSOLE=gamecube make    # → joypad-tester-gamecube.dol
TARGET_CONSOLE=wii      make    # → joypad-tester-wii.dol
```

Without devkitPro, the project ships a Docker wrapper that uses the
`ghcr.io/extremscorner/libogc2` image (libogc2 is required for reliable
N64-controller SI detection):

```
./build_docker.sh gamecube
./build_docker.sh wii
./build_docker.sh clean
```

CI builds both targets on every push to `main` (see
[`.github/workflows/verify-build.yml`](../.github/workflows/verify-build.yml)).

## Banner

`opening.bnr` (the file Swiss displays when browsing the folder) is generated
from `assets/banner.png` by `buildtools/make_banner.py`. Edit
`assets/banner.png` (96×32, RGB or RGBA — alpha composites onto black) and
re-run:

```
python3 buildtools/make_banner.py opening.bnr
```

## Loading on hardware

### GameCube — SD-card loaders (Swiss / GC Loader / FlippyDrive / SD2SP2)

Grab `joypad_tester_gcn_v<ver>.zip` from the release and extract it
to your SD root. The archive contains the Swiss-ready folder layout:

```
SD root/
  Joypad Tester/
    default.dol
    opening.bnr
```

Swiss reads `opening.bnr` and shows the banner image + description in
its file browser; selecting the folder runs `default.dol`.

### GameCube — bootable disc image / Dolphin / IPL-replacement modchips

Grab `joypad_tester_gcn_v<ver>.iso` for a bootable iso9660 + El
Torito disc image. Works for:

- Dolphin (File → Open the `.iso`, or drag-drop)
- DVD-R burn for IPL-replacement modchips (GCOS, Swiss-as-IPL, etc.)
- ODE flash carts that prefer disc images over folder layouts

The ISO uses bushing's open-source homebrew apploader
([gcn/buildtools/apploader/](buildtools/apploader/)) — the binary
content is the same `default.dol` + `opening.bnr` as the ZIP, just
in disc form.

### Wii (Homebrew Channel)

Grab `joypad_tester_wii_v<ver>.dol` and load it through the Homebrew
Channel (either drop into `apps/JoypadTester/boot.dol` on your SD or
send via WiiLoad).

## Releases

Tagged as `gcn-v<semver>` from the repo root — see
[`gcn/CHANGELOG.md`](CHANGELOG.md) for per-version notes. The release
workflow attaches:

- `joypad_tester_gcn_v<semver>.zip` — pre-built Swiss folder layout.
- `joypad_tester_gcn_v<semver>.iso` — bootable disc image (homebrew
  apploader + `.dol` + banner, composed with `mkisofs`).
- `joypad_tester_wii_v<semver>.dol` — bare Wii build.

## Origin / credits

Derived from corenting's
[GC-Controller-Test](https://github.com/corenting/GC-Controller-Test)
under the [zlib license](../LICENSE.md). N64 detection / poll, accessory
probe + rumble actuation + bio sensor BPM, multi-port simultaneous display,
GC keyboard support, banner pipeline, and CI release infra are added on
top. Pak probe sequence and bio sensor BPM math follow libdragon's
[JoypadTest-N64](https://github.com/meeq/JoypadTest-N64) reference. GC
keyboard wire format and scancode mapping come from the
[joypad-os](https://github.com/joypad-ai/joypad-os) firmware.
