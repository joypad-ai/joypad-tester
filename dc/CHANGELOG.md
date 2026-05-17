# Joypad Tester — Dreamcast — Changelog

## v0.2.0 — 2026-05-17

VMU Icon Editor mode is live. Full draw/extract/apply loop wired
end-to-end against real VMU hardware via KallistiOS vmufs.

### Highlights

- **Editor mode** — 32×32 color (16-palette ARGB1555) + 32×32 mono
  canvases, drawable with the unified cursor (pad analog OR DC
  mouse). Tools: paint / erase / fill / pick. 32-deep snapshot
  undo. D-pad cycles current color; L/R triggers swap color/mono
  layer; Y cycles tool. 10x zoom (320×320 px) for the canvas.
- **Save browser mode** — enumerates every save on every detected
  VMU, decodes a thumbnail per save by extracting frame 0 of its
  embedded icon (works for both ICONDATA_VMS and standard
  game-save icons). A loads the icon into the editor; Y applies
  it directly to the source VMU; X refreshes the list. Read-only
  on game saves.
- **Apply path** — Start in the editor opens a target picker
  listing every detected VMU; A writes `ICONDATA_VMS` via
  `vmufs_write`. Backup-on-replace is on by default: an existing
  `ICONDATA_VMS` is stashed into that VMU's library save before
  the new one overwrites.
- **Library save format** — packed `VMUICONS.VMS` container, one
  per VMU, holds up to 16 icon entries in a single file with a
  versioned `"VMIL"` header. Each entry: name, description,
  timestamp, flags, full palette + color/mono bitmaps.
- **VMS format module** — byte-level ICONDATA_VMS encode/decode
  ported from `dreamcast-icon-maker`'s JS implementation; generic
  save icon extract (palette at 0x60, bitmap at 0x80) for any VMS
  file; CRC helper for the library wrapper.
- **Keyboard polling modernized** — switched from the deprecated
  `kbd_state.matrix[]` / `shift_keys` to the modern `cond.keys[]`
  / `last_modifiers.raw` API. Tester display now shows actually-
  pressed scancodes rather than scancode-indexed bitmask leftovers.

### Known follow-ups (v0.2.x)

- DC keyboard + on-screen keyboard for editing the save description
  (canvas stores it; OSK widget needed to enter it without a
  physical keyboard).
- Animated-icon preview for multi-frame saves in the browser.
- Per-action confirmation modal on Apply when backup-on-replace
  fires (currently silent).

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
