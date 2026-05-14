# Joypad Tester — Nintendo 64

Nintendo 64 build of the [Joypad Tester](../README.md). Renders the
live state of every Joybus device on the four N64 controller ports
— including a Game Boy Advance plugged in over a GameCube-to-GBA
link cable + N64-port adapter, multibooted by this very ROM into
JOYBUS mode.

## What it tests

```
== Joypad Tester - N64 ==

P1 id:0500 N64  N64 pad      acc:Rumble stk +12, -4  cstk  +0,+0   L  0 R  0
   A:1 B:0 X:0 Y:0  L:0 R:1 Z:0 Strt:0  D:0000 C:0010
P2 id:0900 GCN  GCN pad      acc:-      stk -85,+82  cstk +30,-12  L 87 R 92
   A:0 B:1 X:1 Y:0  L:1 R:0 Z:0 Strt:0  D:0000 C:0000
P3 id:0004 ---- GBA (Joybus) acc:-      [booted; poll 00 ff]
P4 id:ffff ---- no device    acc:-
```

| Field | Meaning |
|-------|---------|
| `id` | Raw 16-bit Joybus identifier from `joypad_get_identifier()`. The most useful diagnostic — every Joybus device puts a unique value here. |
| Style | LibDragon's high-level classification (N64 / GCN / Mouse / -). |
| Name | Decoded device name (N64 pad / GCN pad / GBA (Joybus) / 64GB Cable / RandNet KB / etc.). |
| `acc:` | N64 controller accessory (Memory Pak / Rumble Pak / Transfer Pak / Bio Sensor / Snap Station). |
| `stk` / `cstk` | Analog sticks (-127..+127); the cstick is GameCube-only — N64 pads have C-buttons instead. |
| `L` / `R` | Analog shoulder triggers (GameCube-only). |
| `A B X Y L R Z Strt` | Digital buttons. Fields not present on the physical pad read 0. |
| `D` / `C` | D-pad and C-button bits (UDLR in each). |

| Source | Detected as |
|--------|-------------|
| Standard N64 controller | `JOYBUS_IDENTIFIER_N64_CONTROLLER` (0x0500) |
| GameCube controller via passive 3-pin adapter | `0x09XX` (the low byte carries rumble / wireless / origin status) |
| GBA in JOYBUS mode via GameCube/GBA link cable + N64-port adapter | `JOYBUS_IDENTIFIER_GBA_LINK_CABLE` (0x0004) |
| Original Game Boy Camera Link Cable | `JOYBUS_IDENTIFIER_64GB_LINK_CABLE` (0x0003) |
| N64 Mouse / RandNet Keyboard / VRU | distinct identifiers (`0x0200` / `0x0002` / `0x0001`) |
| Empty port | `0xffff` |

## GameCube/GBA link cable testing

NUON-Dome users can wire the GameCube end of a DOL-011 link cable to
an N64 controller port (Joybus is the same single-wire 3.3V protocol
on both consoles — only the connector pinout differs; a 3-pin
splice suffices). Plug the GBA end into a powered-on GBA with no
cartridge, and the N64 ROM will:

1. Detect the GBA via the 0x0004 identifier on whichever port the
   cable is wired to.
2. Wait for any pad to press **A** — at which point it sends the
   embedded **`tester_mb.gba`** payload (~57 KB, the same artifact
   the GameCube tester multiboots; see [`../gba/`](../gba/)) via the
   Kawasedo handshake + stream cipher.
3. After ~1-2 s the GBA starts running the joypad tester payload
   on its own screen; the N64 ROM falls through to polling that
   payload's 0x14 status word and shows the live bytes on the P*N*
   row.

The multiboot uploader (`src/gba.c`) is a direct port of the
GameCube version at [`../gcn/ppc/gba.c`](../gcn/ppc/gba.c), with
libogc's `SI_Transfer` swapped for LibDragon's `joybus_exec_cmd`.
Same Kawasedo handshake, same cipher constants, same payload.

## LibDragon toolchain

We build with [LibDragon trunk](https://github.com/DragonMinded/libdragon)
— the actively-maintained modern N64 SDK. Christopher Bonhage
(meeq)'s [JoypadTest-N64](https://github.com/meeq/JoypadTest-N64)
joypad subsystem has been upstreamed into LibDragon trunk, so we
link directly against the in-toolchain version rather than
vendoring meeq's code. The `0x0004` GBA identifier + the
`joypad_get_identifier()` API are LibDragon-native.

The LibDragon Docker image is `ghcr.io/dragonminded/libdragon`,
which ships the MIPS cross-compiler only; our
[`buildtools/Dockerfile`](buildtools/Dockerfile) layers
`./build.sh` on top to install the library + `n64.mk`. Apple
Silicon hosts run it under `--platform=linux/amd64` (libdragon
publishes amd64-only layers).

## Build

```
./build_docker.sh                # Docker-based, no native install needed
./build_docker.sh clean
./build_docker.sh rebuild-image  # bump after libdragon trunk updates
```

`build/joypad-tester.z64` is the in-tree output. Not yet wired into
CI — sits as a working-dir baseline pending hardware validation.

## Loading on hardware

### Emulator

[Ares](https://ares-emu.net) is the recommended modern N64 emulator
— LibDragon's Joybus emulation is the most accurate of the
available options. Load `build/joypad-tester.z64` directly. GBA
multiboot won't fire on emulator (no real Joybus link cable
behind the emulated controller ports), but the standard pad +
GameCube-via-virtual-adapter detection works.

### Real N64 hardware

Flash the .z64 to an EverDrive 64 / 64drive / SummerCart64 SD card
under the `roms/` folder. Power on the N64, navigate to the menu,
select `joypad-tester`. The Joybus identifier of every plugged
device shows up on the four-row display.

For the GBA test:

1. Power on the GBA *without* a cartridge — it'll show the BIOS
   intro and then enter the multiboot wait state.
2. Wire the GameCube end of a DOL-011 link cable to one of the
   N64 controller ports (Joybus pinout: +3.3V, GND, Data — the
   GameCube and N64 connectors use different shells but the same
   electrical contacts).
3. Plug the GBA end of the cable into the GBA's link port.
4. On the N64 display, you'll see one port flip to `id:0004
   GBA (Joybus)`. Press **A** on any connected pad.
5. After ~1-2 s the GBA's screen will switch from BIOS to the
   joypad-tester GBA build; the N64 row will start polling the
   payload's status bytes.

## Releases

Not released yet. The next version will land once the GBA
multiboot path is validated on real hardware (the GameCube version
has been validated; the N64 port is a fresh adaptation of the
same algorithm against LibDragon's Joybus transport).

## Origin / credits

- **Joypad / accessory subsystem**: Christopher Bonhage (meeq)'s
  [JoypadTest-N64](https://github.com/meeq/JoypadTest-N64) (public
  domain), now upstreamed into LibDragon trunk. Detects N64 pads,
  GameCube pads via passive adapter, Rumble Pak, Controller Pak,
  Transfer Pak, Bio Sensor, Snap Station, GB Camera.
- **GBA multiboot uploader**: ported from
  [`../gcn/ppc/gba.c`](../gcn/ppc/gba.c) in this repository, itself
  ported from
  [Doridian/Joybus-PIO](https://github.com/Doridian/Joybus-PIO) +
  [AxioDL/jbus](https://github.com/AxioDL/jbus). MIT.
- **GBA payload**: built from `../gba/build/tester/tester_mb.gba`
  (this repo's GBA subdir). MIT.
- **LibDragon SDK**: public domain, from
  https://github.com/DragonMinded/libdragon.

See [`LICENSE.md`](LICENSE.md) for the full per-file provenance.
