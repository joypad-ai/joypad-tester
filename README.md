# Joypad Tester

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/logo_solid.svg">
    <source media="(prefers-color-scheme: light)" srcset="assets/logo_solid_black.svg">
    <img alt="Joypad Tester" src="assets/logo_solid_black.svg" width="300">
  </picture>
</p>
<p align="center">
  <strong>Open-Source Controller Test ROMs for Retro Game Consoles</strong>
</p>
<p align="center">
  Drop a single ROM on a flash cart and inspect every controller,<br>accessory, and protocol quirk the console can speak to.
</p>
<p align="center">
  <a href="https://github.com/joypad-ai/joypad-tester/releases"><img src="https://img.shields.io/github/downloads/joypad-ai/joypad-tester/total?style=for-the-badge&label=Downloads" alt="Downloads" /></a>
  <a href="https://github.com/joypad-ai/joypad-tester/blob/main/LICENSE.md"><img src="https://img.shields.io/badge/License-MIT-blue?style=for-the-badge" alt="License" /></a>
  <a href="https://github.com/joypad-ai/joypad-tester/actions/workflows/verify-build.yml"><img src="https://img.shields.io/github/actions/workflow/status/joypad-ai/joypad-tester/verify-build.yml?style=for-the-badge&label=CI" alt="CI Status" /></a>
  <a href="http://community.joypad.ai/"><img src="https://img.shields.io/discord/1133112432684978256?style=for-the-badge&logo=discord&label=Discord" alt="Discord" /></a>
</p>

---

A collection of homebrew controller test ROMs across game consoles. Each console gets its own self-contained subdirectory with its own toolchain and build.

## Consoles

| Console | Status | Path | License |
|---|---|---|---|
| GameCube | working | [`gcn/`](gcn/) | [zlib](gcn/LICENSE.md) |
| Game Boy Advance | working | [`gba/`](gba/) | [MIT](gba/LICENSE.md) |
| PC Engine | working | [`pce/`](pce/) | [MIT](pce/LICENSE.md) |
| 3DO | working | [`3do/`](3do/) | [MIT](3do/LICENSE.md) |
| Dreamcast | working | [`dc/`](dc/) | [MIT](dc/LICENSE.md) |
| Nintendo 64 | working | [`n64/`](n64/) | [MIT](n64/LICENSE.md) |

Subdirs use short 3-letter codenames (`gcn`, `gba`, `pce`, `dc`, `n64`,
future `snes`/…) matching homebrew-community conventions. Release tags
follow the same prefix: `<codename>-v<semver>`.

## Acknowledgements

Each console app has its own lineage. As more consoles join, this list grows.

| Console | Origin / inspiration |
|---|---|
| GameCube | Derived from [corenting/GC-Controller-Test](https://github.com/corenting/GC-Controller-Test) (zlib). Multi-port layout and accessory probe flow modeled after [meeq/JoypadTest-N64](https://github.com/meeq/JoypadTest-N64). GC keyboard wire format and scancode table come from the [joypad-os](https://github.com/joypad-ai/joypad-os) firmware (`src/lib/joybus-pio`). |
| Game Boy Advance | Joybus handshake + main loop from [Doridian/Joybus-PIO](https://github.com/Doridian/Joybus-PIO) (MIT). Eyes overlay ported from [joypad-os](https://github.com/joypad-ai/joypad-os)'s `eyes_anim`. Mode-4 page-flipped screensaver matches the GameCube tester's logo + color cycle. Two ROM variants from one source tree: `joypad_mb.gba` (eyes, for joypad-os submodule consumers) and `tester_mb.gba` (Doridian + on-GBA console). |
| PC Engine | Baseline from [dshadoff/PCE_Mouse_Test](https://github.com/dshadoff/PCE_Mouse_Test) (MIT). Detects 2-button / 6-button pads and the PC Engine mouse, including multitap support. Built with [uli/huc](https://github.com/uli/huc) (HuC) pinned in a Docker image we maintain. |
| 3DO | Fresh implementation against [trapexit/3do-devkit](https://github.com/trapexit/3do-devkit) (ISC). Detects every PBUS device on the daisy chain (Pad, Mouse, Stick, LightGun, Arcade SillyPad, Keyboard) by talking directly to the Portfolio event broker. Bundles a rebuilt broker daemon from [trapexit/portfolio_os](https://github.com/trapexit/portfolio_os) with every driverlet static-linked in, plus a new `SillyPadDriver.c` for the 0xC0 silly-pad and a finished `KeyboardDriver.c` (PR open upstream). Conceptual ancestor: Charles Doty (RasterSoft)'s [3DO Controller Test Multi](https://3dodev.com/software/homebrew/console_tools) (2016). Pinned devkit commit lives in `3do/buildtools/Dockerfile`. |
| Dreamcast | Fresh implementation against [KallistiOS](https://github.com/KallistiOS/KallistiOS) (BSD-style). Reads every maple-bus device class — controllers, mice, keyboards, light guns, plus per-port slot peripherals (VMU, Purupuru, microphone). Also hosts a built-in VMU icon editor mode (v0.2+) ported from [RobertDaleSmith/vmu-icon-maker](https://github.com/RobertDaleSmith/vmu-icon-maker) (MIT). First console in the repo to ship the mode-switching scaffold (Options menu via Start+Down) that future testers can adopt. Pinned KOS commit lives in `dc/buildtools/Dockerfile`. |
| Nintendo 64 | Fork of [meeq/JoypadTest-N64](https://github.com/meeq/JoypadTest-N64) (public domain) on top of [LibDragon trunk](https://github.com/DragonMinded/libdragon) (also public domain). Detects N64 pads, GameCube pads (passive 3-pin adapter), the N64 Mouse, the Voice Recognition Unit, Rumble / Controller / Transfer Pak, Bio Sensor (live BPM streaming), Snap Station (protocol exerciser), and GBA-in-JOYBUS-mode via the GameCube / GBA link cable — with Kawasedo multiboot ported from `gcn/ppc/gba.c` so the same `tester_mb.gba` payload the GameCube tester boots will boot a GBA over the N64 controller port. Secondary modes (Controller Pak browser, GB Camera viewer, Snap Station) follow the dc/ Options-menu scaffold. |

Originating copyrights are preserved in each console's source headers.

## License

Top-level repo scaffolding (CI, build infra, this README): [MIT](LICENSE.md).
Each console subdir carries its own `LICENSE.md` matching its upstream
origin — see the **License** column of the Consoles table above.
