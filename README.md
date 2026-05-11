# Joypad Tester

A collection of homebrew controller test ROMs across game consoles. Each console gets its own self-contained subdirectory with its own toolchain and build.

## Consoles

| Console | Status | Path |
|---|---|---|
| GameCube | working | [`gamecube/`](gamecube/) |
| Game Boy Advance | working | [`gba/`](gba/) |

## Per-console

Each console subdir has its own `README.md` with what its app tests,
how to build/flash, and any embedding notes. See:

- [`gamecube/README.md`](gamecube/README.md)
- [`gba/README.md`](gba/README.md)

## Acknowledgements

Each console app has its own lineage. As more consoles join, this list grows.

| Console | Origin / inspiration |
|---|---|
| GameCube | Derived from [corenting/GC-Controller-Test](https://github.com/corenting/GC-Controller-Test) (zlib). Multi-port layout and accessory probe flow modeled after [meeq/JoypadTest-N64](https://github.com/meeq/JoypadTest-N64). GC keyboard wire format and scancode table come from the [joypad-os](https://github.com/joypad-ai/joypad-os) firmware (`src/lib/joybus-pio`). |
| Game Boy Advance | Joybus handshake + main loop from [Doridian/Joybus-PIO](https://github.com/Doridian/Joybus-PIO) (MIT). Eyes overlay ported from [joypad-os](https://github.com/joypad-ai/joypad-os)'s `eyes_anim`. Mode-4 page-flipped screensaver matches the GameCube tester's logo + color cycle. Two ROM variants from one source tree: `joypad_mb.gba` (eyes, for joypad-os submodule consumers) and `tester_mb.gba` (Doridian + on-GBA console). |

Originating copyrights are preserved in each console's source headers.

## License

Top-level repo (CI, build infra, this README): [MIT](LICENSE.md). Each
console subdir carries its own license file matching its upstream
origin — `gamecube/` is zlib via corenting's GC-Controller-Test (see
[`gamecube/LICENSE.md`](gamecube/LICENSE.md)); `gba/` is MIT via
Doridian's Joybus-PIO (see [`gba/LICENSE.md`](gba/LICENSE.md)).
