# Joypad Tester — NUON — Changelog

## v0.1.0 — 2026-05-14

First release. Initial bring-up of the NUON controller test ROM via
the cubanismo/nuon-sdk toolchain (the modern repackaged VM Labs SDK),
signed for retail-hardware boot using the authentication keys
released by AltRN8 in 2022.

### Highlights

- Live 4-port readout of every documented `_Controller[]` field:
  A/B/L/R, Start, Z (Select), D-pad (Up/Down/Left/Right), C-pad
  (CUp/CDown/CLeft/CRight), analog X / Y, `status`, raw
  `properties` word, and the four-byte tail (`xAxis2 / yAxis2 /
  spinner1 / spinner2`).
- IR remote bit dump — full 32-bit `remote_buttons` field broken out
  into per-bit display, so users can identify which physical remote
  button on which NUON-enabled DVD player maps to which bit.
- Signed `nuon.run` payload at `/NUON/nuon.run` on the release ISO —
  boots on retail Samsung N501/N504/N505, RCA DRC300N/DRC480N, and
  Toshiba SD2300 (via the macOS `hdiutil` Universal-format variant
  also attached to each release).

Full feature breakdown + build / loading instructions in
[`nuon/README.md`](https://github.com/joypad-ai/joypad-tester/blob/main/nuon/README.md).
