MIT License

Copyright (c) 2026 Robert Dale Smith

The Joypad Tester NUON ROM source (`src/main.c`) is original work
licensed under MIT. It exercises the publicly-documented BIOS
joystick API (`_Controller[]`, `_DeviceDetect()`, the `ButtonX()`
macros from `nuon/joystick.h`) but does not copy or redistribute any
VM Labs SDK source.

The build pipeline depends on
[cubanismo/nuon-sdk](https://github.com/cubanismo/nuon-sdk) — a
repackaged amalgamation of the original VM Labs NUON SDK Linux and
Windows tools (release 0.86.2, 2001), plus the DVD authentication /
"blessing" framework rediscovered and ported to modern hosts by
AltRN8, EdgeConnector, and mgarcia after the signing keys surfaced
in 2022. The SDK is consumed at build time only — none of its
source ships in this repository.

The original VM Labs SDK headers (`nuon/*.h`) are copyright
1995-2001 VM Labs, Inc., marked "Confidential and Proprietary
Information". VM Labs was acquired by Genesis Microchip in 2001 and
the SDK was never formally open-sourced; the NUON homebrew
ecosystem (NUON-Dome, Skah_T's Boot Loader, the Nuance/
NuonResurrection emulator, etc.) operates on the abandonware footing
the surrounding industry has settled into for 20+ years. We consume
these headers via the toolchain at compile time; we don't
redistribute them.

---

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
