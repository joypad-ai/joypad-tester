# Joypad Tester — Dreamcast — Changelog

## v0.1.1 — 2026-05-17

Smoke-tested on Flycast. Polishes the controller tester and wires
the previously-stubbed peripheral exercises.

### Highlights

- Tester slot label now shows live VMU free-block count (`VMU 196`
  format) refreshed every ~0.5s via `vmufs_free_blocks` +
  `vmufs_root_read`.
- Hold **B** on a port with a VMU writes the Joypad Tester "JT"
  wordmark to that VMU's LCD via `vmu_draw_lcd_xbm`.
- Hold **Y** on a port with a VMU clock reads `vmu_get_datetime` and
  displays `S<n> RTC: YYYY-MM-DD HH:MM:SS` beneath the port row.
- Render pipeline fixes for single-buffered framebuffer flicker:
  bfont draws in opaque mode (per-glyph background repaint, no
  per-frame `memset`); options menu and active mode are
  mutually-exclusive layers per frame instead of racing; vblank wait
  happens before draw rather than after.
- Per-port row format trimmed to 50 chars to fit in 640px and stop
  wrapping onto the next port's row.
- About page layout: URL moved up 32px so it stops overlapping the
  footer hint.

## v0.1.0 — 2026-05-14

First release. Bring-up of the Dreamcast joypad tester app via the
KallistiOS toolchain, with a mode-switching scaffold that hosts the
controller tester as the landing screen and reserves slots for the
VMU Icon Editor + About views (unlocked in v0.2).

### Highlights

- Live readout of all four maple ports (A/B/C/D), each with both
  expansion slots probed for VMU / Purupuru / Microphone subdevices.
- Per-port style detection: controller, mouse, keyboard, light gun
  (extensible — Maple function-mask driven, not hardcoded).
- Hold **A** on any port whose slot has a Purupuru pack to actuate
  rumble; hold **B** to write the Joypad logo to a VMU's LCD; hold
  **Y** to read the VMU clock subdevice. Mirrors the gcn/ tester's
  hold-A rumble convention.
- VGA-first video: detects the cable type at boot and selects
  `DM_640x480_VGA` when present, falling back to NTSC/PAL interlaced
  modes otherwise. Cable + region surfaced on the About screen.
- Options menu (hold **Start + Down**, or click the mouse menu chip)
  with three modes: Controller Tester (default), VMU Icon Editor
  (stub in v0.1), About. Mode registry is data-driven so future
  testers can adopt the same scaffold without churning core.
- Self-boot `.cdi` disc image suitable for GDEMU / GDROM-replacement
  / CD-R, with IP.BIN marked VGA-compatible.

Full feature breakdown + build / loading instructions in
[`dc/README.md`](https://github.com/joypad-ai/joypad-tester/blob/main/dc/README.md).
