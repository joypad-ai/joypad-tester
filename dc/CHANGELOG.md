# Joypad Tester — Dreamcast — Changelog

## v1.0.0 — 2026-05-27

First public release. The 0.2.x line rounded out feature parity with
the web `dreamcast-icon-maker`; 1.0 stabilises the rendering pipeline
and lands the workflow polish the project needs to be useful as a
real Dreamcast utility (and a controller tester) on hardware.

### Highlights

- **Double-buffered rendering** — `DM_MULTIBUFFER` + `vid_flip`. Full
  clear + redraw per frame, no beam-race flicker anywhere. Deleted
  all the single-buffer machinery (cursor save-restore, dirty-flag
  signatures, cell-highlight tracking, picker/menu/OSK redraw gating,
  union-paint screensaver).
- **VMU File Manager redesign** — top-level VMU list with the BIOS
  icon thumbnail wrapped in the VMU's custom-color border (or the
  preset BIOS icon shape when there's no `ICONDATA_VMS`). Drill into
  a VMU's saves; **A** opens a details popup, **Y** extracts to the
  editor, **X** deletes (with a confirm modal). Per-VMU **Options**
  modal — palette-editor-style sliders for the BIOS color, L/R
  toggles for Real Mode + Custom Color, **A** saves / **B** cancels,
  **Y** syncs the VMU clock to the console time. Auto-writes a
  default `ICONDATA_VMS` (the Joypad logo) when toggling Real Mode
  on a VMU that has none.
- **Live VMU LCD mirror** — in the Controller Tester, the slot-1 VMU
  continuously renders the controller's D-pad / face buttons /
  triggers / analog stick. In the Icon Editor, every connected VMU
  mirrors the live mono canvas. Other modes restore the stored
  ICONDATA LCD icon on leave.
- **Controller subtype detection** — `Type:` column on the tester
  reads the maple-reported `product_name` and maps Twin Stick /
  Arcade Stick / Ascii Stick / Mission Stick / Fishing Controller /
  Wheel / Pop'n / Densha / Maracas / DreamConn / Beat Mania / Samba
  / Guitar to short labels. C/D/Z bits are now rendered alongside
  A/B/X/Y/Start/D-pad.
- **Editor workflow polish** — save flow is a 2-step wizard (pick
  VMU → Apply-as-current-icon vs Store-in-library), with the
  destination mode auto-loading after. Real Mode is now a VMU-level
  toggle in the File Manager (auto-preserved across icon swaps);
  the editor button is gone. D-pad nudges the cursor with
  hold-to-repeat. Alpha is rendered via a transparency checkerboard
  on the canvas, swatches, and palette-editor preview.
- **Branded library icon** — the `VMUICONS.VMS` library save carries
  a baked Joypad-logo BIOS icon. Asset pipeline: drop a new
  `ICONDATA_VMS` into `assets/library_icon/` and the Makefile
  regenerates `gen_library_icon.h` automatically.
- **Rumble fixed on hardware** — old raw value left `motor = 15`,
  rejected by real Purupuru packs (emulator was ignoring it). Now
  uses the structured `purupuru_effect_t` with `motor = 1`.
- **CRT-safe layout** — every page's header/footer pulled into the
  ~y=24..452 safe band so overscan no longer clips them.
- **Bug fixes** — file-manager thumbs no longer crop to top-left
  24×24; OSK no longer types a stray key from the picker's
  confirming A; menu-close artifacts gone; mono palette panel
  matches the web app's VMU-LCD look; mono-invert correctly trips
  the dirty hash (and is moot under double buffering anyway).

## v0.2.2 — 2026-05-17

Polish + parity round. Closes the remaining web-app feature gaps
that v0.2.1 acknowledged: palette editing, color swap, and animated
multi-frame icon preview.

### Highlights

- **Palette color editor** — press **X** while the cursor is over
  any color swatch in the editor's palette strip to open a 4-channel
  ARGB editor overlay. D-pad up/down picks the channel (R/G/B/A),
  left/right adjusts the value (0..15, matching the 4-bit on-disc
  precision). Live preview swatch updates as you scroll. A
  commits + pushes an undo snapshot; B reverts.
- **Color swap tool** — new tool (Y cycles `Paint → Erase → Fill →
  Pick → Swap`). A on a pixel in the color canvas replaces every
  pixel of that source color with the current color globally —
  one-click recolor.
- **Animated multi-frame icon preview** — Save Browser now decodes
  all 1..3 frames from each save (was just frame 0) and cycles the
  thumbnail at the declared `anim_speed` (header offset 0x42).
  Rows with multi-frame icons get a `~` indicator after the
  port/slot prefix.

### Deferred to v0.3

- Image import (PNG/BMP → 32×32 quantized to the active palette).
  Requires a kos-ports libpng dependency; weighing whether to vendor
  a smaller PNG decoder instead.
- Eyecatch (cover-art) decode/display for game saves.
- DreamPi network upload/download against the existing PHP backend.

## v0.2.1 — 2026-05-17

Editor reworked to match the web `dreamcast-icon-maker`'s two-pane
UX. Adds the on-screen keyboard, save-to-library flow, and the
library browser that closes the read-back gap from v0.2.0.

### Highlights

- **Two-pane editor** — color and mono canvases visible
  simultaneously at 6× zoom (192×192 each). Cursor-region-sensitive
  input: A paints in whichever pane the cursor is over, B erases.
  Mirrors the web app's side-by-side layout.
- **Mono palette toggle row** — 16 cells, one per color-palette
  index. Clicking a cell flips every mono pixel of that color
  (jt_canvas_mono_toggle_palette). Direct color→silhouette
  translation, matching the web app's `monoPaletteStates`.
- **Per-canvas Reset buttons** + **mono Invert button** + **Real
  Mode toggle button** with on/off indicator.
- **On-screen keyboard widget** (src/ui/osk.c) — modal 6×10 grid
  with 3 layers (lower / UPPER / symbols), driven by cursor /
  D-pad / A, or by a real maple keyboard plugged in (chars flow
  through the same input path). Triggered by the editor's Name
  button.
- **Save-to-library** flow in the editor — picks target VMU,
  prompts for entry name via OSK, appends to that VMU's
  `VMUICONS.VMS`.
- **Library Browser mode** (new) — read-back UI for library saves
  across all VMUs. A loads selected entry into editor; B prompts
  to delete; X refreshes. Entries flagged with `*` are auto-
  backups (stashed by Apply with backup-on-replace); `R` marks
  Real-Mode-enabled icons.
- **Save Browser** + **Library Browser** are now distinct modes
  in the options menu (Controller Tester / VMU Icon Editor / VMU
  Save Browser / Library Browser / About).

### Deferred to v0.2.2

- Animated-icon preview in the Save Browser (multi-frame cycling).
- Palette color editing (currently fixed to the 16 default colors;
  web app lets the user edit each entry).
- Image import (PNG/BMP → 32×32 quantized to the active palette).

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
