# Joypad Tester — Dreamcast — Changelog

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
