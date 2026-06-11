# Joypad Tester — Xbox

Original Xbox build of the [Joypad Tester](../README.md). Reads every
USB peripheral the console can talk to through its four controller
ports — pads (Duke / S / Steel Battalion / Arcade Stick / wheel),
daisy-chained second pads, in-slot Memory Units, Voice Communicator /
wired headset / mic, the DVD Movie Playback Kit IR receiver, USB
keyboard and mouse — and renders the live state of all four ports
simultaneously. Analog button pressure, dual-motor rumble, and a real
on-screen mouse cursor all work on hardware.

## What it tests

<p align="center">
  <em>screenshot.png — placeholder, will land once v1.0 has on-hardware caps</em>
</p>

All four controller ports (1–4) rendered live, simultaneously, with no
active-port toggle. Each port shows whatever pad / accessory is on it
plus both expansion slots:

```
Port 1  Type:Duke         Slot1:MU 8MB        Slot2:Mic        Rumble: idle
Stick: L+000,+000  R+000,+000   Trig: L000 R000
A:000 B:000 X:000 Y:000  Wh:000 Bk:000        (0..255 pressure)
D-U:0 D-D:0 D-L:0 D-R:0  Back:0 Start:0  LSB:0 RSB:0

Port 2  Type:Mouse        (USB HID report protocol)
Btns:  L:0 R:0 M:0   dx:+000 dy:+000  Wheel:+000
Cursor: 320,240  WheelTotal:+0000

Port 3  Type:DVD Remote   (XREMOTE IR receiver)
Last button: Play    (12 ms ago)

Port 4  Type:Empty
```

| Type             | Source                                                        |
|------------------|---------------------------------------------------------------|
| Duke / S / etc.  | XID gamepad (`bType` 1 / 2 / 0x80 / 0x20 / 0x10)              |
| Daisy pad        | Second XID gamepad chained off a controller's expansion slot  |
| Mouse            | USB HID boot-protocol mouse (`subclass 1 / protocol 2`)       |
| Keyboard         | USB HID boot-protocol keyboard (`subclass 1 / protocol 1`)    |
| DVD Remote       | XREMOTE IR receiver (`bType 0x03`)                            |
| Steel Battalion  | Steel Battalion controller (`bType 0x80`)                     |
| Empty            | No device on this port                                        |

| Slot device      | Source                                                  |
|------------------|---------------------------------------------------------|
| MU \<size\>      | USB Mass Storage class (Xbox MU; `class 0x08`)          |
| Mic / Headset    | USB Audio class (`class 0x01`) OR Xbox class `0x78`     |
| Hub              | USB hub class on the slot                               |
| ---              | Empty slot                                              |

Fields a peripheral doesn't have (e.g. analog pressure on a keyboard)
stay at zero — same convention as the dc/ and gcn/ tester.

## Using it

### Options menu

Hold **Start + Down** at any time to open the menu:

```
┌─ OPTIONS ─────────────────┐
│  > Controller Tester      │
│    About                  │
│    Return to Dashboard    │
└───────────────────────────┘
```

D-pad navigates, **A** confirms, **B** closes. The third row fires the
same `XLaunchXBE(NULL)` dashboard-return path as the in-game-reset
combo.

### In-game reset combo

Hold **Back + Black + LT + RT** on any pad for ~1 second to return to
the dashboard. Matches the standard OG Xbox "quit to dash" gesture
(e.g. Halo 2 quit). LT/RT have to clear ~200/255 pressure to register,
so the combo can't fire accidentally during normal play.

### Screensaver

After 2 minutes of no input on any pad, keyboard, mouse, DVD remote, or
Steel Battalion, a bouncing logo screensaver takes over. Any input
wakes it.

## Analog button pressure

Duke / S pads ship six analog face buttons (A / B / X / Y / Black /
White) that report 0–255 pressure, not just digital state. SDL2's
gamecontroller layer on nxdk only exposes the digital bits, so the
tester reads pressure directly off the XID device's interrupt-IN
transfer buffer (`xid_dev->utr_list[]->buff` bytes 4–9). The same
buffer also drives the rumble logic below.

## Pressure-driven rumble

The Duke / S pad has two motors — a heavy/low-frequency one in the
left grip and a light/high-frequency one in the right. **A** drives
the left motor and **B** drives the right, with intensity proportional
to the analog pressure on that button: feather A for a soft rumble on
the left, press hard for max. Same scheme on the daisy pad
independently.

## Mouse cursor + wheel

An 8×12 white arrow renders on top of every screen and integrates the
mouse's motion in the HID interrupt-IN callback — no dropped dx/dy
between frames or between USB reports. The cursor coordinates and a
cumulative `WheelTotal` are shown in the mouse block.

The wheel pipeline does proper HID-report-descriptor parsing instead
of forcing boot protocol (which is officially a 3-byte report — no
wheel). At probe time we fetch the report descriptor via
`usbh_hid_get_report_descriptor()`, scan it for a `REPORT_ID` global
item, switch the mouse into report-protocol mode, and parse each
report as `[report_id?][btns][dx][dy][wheel]` with the correct offset.
Keyboards stay in boot protocol — their 8-byte spec doesn't need it.

## USB hub support

A controller, keyboard, and mouse plugged into a generic USB hub on a
single controller port all render under that port — the tester walks
each device's parent `UDEV_T` chain to resolve hub topology back to
the chassis port the hub is plugged into. PSO-style "USB-to-Xbox"
adapter cables (controller-slot wires that expose the slot as a USB
port) work too.

## Output mode (About page)

The About page reports both:

- **Render** — the internal framebuffer (`640×480`, set by
  `XVideoSetMode`).
- **Output** — the actual TV signal decoded from
  `XVideoGetEncoderSettings()`: cable type (Composite / SCART RGB /
  S-Video / Component / VGA), TV region (NTSC-M / NTSC-J / PAL),
  scan mode (480i / 480p / 720p / 1080i), and refresh (50 / 60 Hz).

A SCART CRT receiving 480i will read `Output: SCART RGB  PAL  480i
50Hz` even though the framebuffer is 640×480 — the encoder
downscales / interlaces our render surface.

## nxdk toolchain

OG Xbox homebrew is built with [nxdk](https://github.com/XboxDev/nxdk)
(clang + lld + pbkit + SDL2). nxdk's prebuilt
`ghcr.io/xboxdev/nxdk:latest` image leaves the libusbohci Mass Storage,
Audio, and HID class drivers unbuilt. We extend it via
[`buildtools/Dockerfile`](buildtools/Dockerfile) which re-runs the
nxdk-internal build with `NXDK_USB_ENABLE_MSC=y / UAC=y / HID=y` so
those class drivers are baked in — without them we can't see Memory
Units, the Voice Communicator / mic, or USB keyboards / mice on real
hardware.

## Build

```
./build_docker.sh                # build (first run also builds image)
./build_docker.sh clean          # nuke build/
```

In-tree outputs:
- `build/default.xbe` — the executable. FTP into any
  `E:\Apps\Joypad Tester\` softmod app folder.
- `build/joypad_tester_xbox_v<ver>.iso` — XISO image for DVD-R burns
  and softmod ISO loaders.

CI builds on every push to `main` (see
[`.github/workflows/verify-build.yml`](../.github/workflows/verify-build.yml)).

## Loading on hardware

### Emulator (xemu)

Drop `joypad_tester_xbox_v<ver>.iso` onto [xemu](https://xemu.app).
xemu 0.8.x emulates Duke and S Controller plus generic USB hub /
keyboard / mouse, so the controller-tester, hub, and HID code paths
all verify there. Memory Units, the DVD Remote, the mic, Steel
Battalion, and steering wheels need real hardware (or USB
passthrough of a real device) — xemu doesn't model them today.

### Real Xbox (softmod + FTP)

FTP `build/default.xbe` into `E:\Apps\Joypad Tester\default.xbe` and
launch from the softmod dashboard's Apps menu.

### Burned DVD-R (or softmod ISO loader)

Burn `joypad_tester_xbox_v<ver>.iso` to a dual-layer DVD-R (CMC PRO MID
recommended) and boot it from a softmodded console, or point an ISO
loader at it.

## Releases

Tagged as `xbox-v<semver>` from the repo root — see
[`xbox/CHANGELOG.md`](CHANGELOG.md) for per-version notes. The release
workflow attaches both `joypad_tester_xbox_v<semver>.zip` (Apps-folder
layout) and `joypad_tester_xbox_v<semver>.iso` to each GitHub Release.

## Origin / credits

The Joypad Tester source under `xbox/src/` is original.
[nxdk](https://github.com/XboxDev/nxdk) and its bundled libusbohci
(N9H30 USB host stack, Nuvoton 2017) used under their respective
licenses. See [`LICENSE.md`](LICENSE.md).
