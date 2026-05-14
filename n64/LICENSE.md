MIT License

Copyright (c) 2026 Robert Dale Smith

The Joypad Tester N64 ROM source is a blend of:

- **Our delta** (`src/gba_joybus.c`, `src/gba_multiboot.c`,
  `src/gba_payload.c`, `src/main.c`, plus the Makefile and Docker
  build pipeline) — MIT-licensed, original work. The GBA Kawasedo
  handshake + stream cipher is ported from
  [`gcn/ppc/gba.c`](../gcn/ppc/gba.c) in this repository (also MIT).
- **meeq's JoypadTest-N64 baseline** (`src/joypad.c`,
  `src/joypad_accessory.c`, `src/joybus_n64_accessory.c`,
  `src/bio_sensor.c`, plus matching headers in `include/`) — released
  to the public domain by Christopher Bonhage under the Unlicense.
- **LibDragon** (the N64 SDK we link against at build time) —
  released to the public domain under the Unlicense by
  Shaun Taylor + the LibDragon contributors.
- **The embedded GBA payload** (`src/gba_payload.c`) — auto-generated
  from [`gba/build/tester/tester_mb.gba`](../gba/build/tester) in
  this repository, MIT-licensed. The payload is the joypad-tester
  GBA ROM built from
  [Doridian/Joybus-PIO](https://github.com/Doridian/Joybus-PIO)-
  derived source (see [`gba/LICENSE.md`](../gba/LICENSE.md)).

The Unlicense (used by meeq and LibDragon) is reproduced below
for the baseline files we vendor; the MIT terms for our delta
follow.

---

## Unlicense (meeq + LibDragon vendored portions)

This is free and unencumbered software released into the public
domain.

Anyone is free to copy, modify, publish, use, compile, sell, or
distribute this software, either in source code form or as a
compiled binary, for any purpose, commercial or non-commercial,
and by any means.

In jurisdictions that recognize copyright laws, the author or
authors of this software dedicate any and all copyright interest
in the software to the public domain. We make this dedication for
the benefit of the public at large and to the detriment of our
heirs and successors. We intend this dedication to be an overt
act of relinquishment in perpetuity of all present and future
rights to this software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY
CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

For more information, please refer to <http://unlicense.org>

---

## MIT License (our delta)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
