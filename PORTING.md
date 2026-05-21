# Joypad Tester — Port Spec

The canonical guide for building a Joypad Tester for a new console.
Read this start-to-finish before adding a port; the
[`/joypad-port`](.claude/skills/joypad-port/SKILL.md) skill follows it
step by step. `CLAUDE.md` carries a condensed copy of these
conventions for in-repo agent work — **this file is authoritative**;
if the two ever disagree, fix `CLAUDE.md` to match here.

The goal: anyone (human or agent) can pick a console and produce a
tester that feels like it belongs in the set — same look, same
feature shape, same parsing philosophy — without re-deriving the
patterns each time.

---

## 0. What a Joypad Tester is

One self-contained test ROM per console. Its job:

> **Detect anything that plugs into the controller port(s) and passes
> input through to the console, and show — live, on screen — exactly
> what it feeds the console.**

Design pillars, in priority order:

1. **The controller tester is the landing screen.** It opens directly
   into a per-port live readout of every connected device. This is the
   core product.
2. **All ports, always live, no active-port toggle.** Every port
   renders simultaneously.
3. **Simple format for the common case.** A device's normal state
   (buttons / sticks / triggers / deltas / scancodes) reads at a
   glance. Fields a device doesn't have **stay at zero — never blank
   them out** (so "zeros" is unambiguous).
4. **Deep features go behind a modal options menu.** Anything richer
   than a live readout (an icon editor, a memory-card browser, a photo
   viewer, a protocol exerciser) is a *secondary mode* you switch to.
   The Dreamcast **VMU Icon Editor** is the template.
5. **Decode to human-readable wherever the mapping is known.** Show
   `A`, `Space`, `Shift` — not raw scancodes — when a documented table
   exists. If it doesn't, show the raw value and flag it; don't
   fabricate a mapping.
6. **Pass-through accessories are in scope.** Anything that sits
   between the controller and the console (memory paks, rumble paks,
   transfer paks, bio sensors, link cables, microphones, VMUs,
   multitaps) gets detected and its data surfaced inline, and may earn
   a secondary mode if it's rich enough.

If a device plugs into the controller port and the console can read
it, the tester should recognise it and show what it's sending.

---

## 1. Repository shape

```
<console>/                 short 3-letter codename (gcn, gba, pce, dc, n64, nuon…)
├── VERSION                bare semver, e.g. "1.0.0"
├── CHANGELOG.md           per-version notes; "## v<semver> — <date>"
├── LICENSE.md             the upstream code's licence
├── README.md             audience-facing (feature-first; see §10)
├── Makefile               build entrypoint
├── build_docker.sh        Docker wrapper (build / clean / rebuild-image)
├── buildtools/Dockerfile  pinned toolchain
├── assets/logo.png        screensaver/About logo source
├── buildtools/make_logo.py  logo -> gen_logo.h packer
└── src/ (or ppc/, source/…)
    ├── main.c             mode dispatcher + idle/screensaver tick
    ├── app.h              mode enum + jt_mode_t contract
    ├── ui/                text helpers, options_menu, screensaver
    └── modes/             one file per mode (tester first, About last)
```

- **Codenames are 3-letter, lowercase**, matching homebrew-community
  usage (`gcn`, `gba`, `pce`, `dc`, `n64`, `nuon`, future `nes`,
  `snes`, `xbox`). The same codename is the release-tag prefix
  (`<codename>-v<semver>`). Never use full names (`gamecube/`) or
  2-letter names (`gc` collides with vendored paths).
- A new port should touch **only its own subdir** plus the few shared
  files in §9.

---

## 2. Required files

| File          | Purpose |
|---------------|---------|
| `VERSION`     | Bare semver. Must match the release tag. |
| `CHANGELOG.md`| One `## v<semver> — <date>` section per release: 1-paragraph summary + a short Highlights list. The GitHub Release body is just a one-line pointer to this file. |
| `LICENSE.md`  | Whatever the upstream code's licence is (zlib, MIT, BSD, public domain…). |
| `README.md`   | Feature-first (see §10). |
| `Makefile`    | Build entrypoint; Docker-based toolchain. |

---

## 3. Visual system

Match these exactly. When unsure, copy from `dc/` or `n64/`, not from
instinct.

### 3.1 Color roles

Every port uses the same palette roles (named `JT_COL_*` in the port's
text helper). Tune the literal values to the platform's color format,
but keep the *roles*:

| Role     | Use                                          | Reference (RGBA) |
|----------|----------------------------------------------|------------------|
| TITLE    | page title row, menu hover                    | yellow `0xfff040ff` |
| FOOTER   | bottom "Start: options menu" hint             | green  `0x80ff80ff` |
| LABEL    | field labels                                  | light grey `0xc0c0c0ff` |
| VALUE    | field values                                  | white `0xffffffff` |
| HELD     | a held button / active cell                   | yellow accent `0xffe040ff` |
| ACTIVE   | connected / active state                      | green `0x80ff80ff` |
| DIM      | disconnected / unheld / zero                  | dark grey `0x606060ff` |
| ERROR    | error states                                  | red `0xff6060ff` |
| CYAN     | secondary highlight (About credits)           | cyan `0x40e0e0ff` |

### 3.2 Title + footer

- **Title row:** the page title in TITLE color, **centered**, including
  the version, e.g. `Joypad Tester - <Console> v1.0.0`.
- **Footer hint:** FOOTER color, **centered**, pinned to the bottom
  row, *inside TV overscan* (don't hug the very last scanline — N64
  taught us to leave ~20px). Text: `Start: options menu` everywhere
  except the controller tester, which uses `Hold Start+Down for
  options menu` (so plain Start stays a testable button there).
- Both must fit the screen's character budget without wrapping.

### 3.3 Per-port readout (the landing screen)

All ports rendered live and simultaneously. Each port is a small block:

```
Port N  Type: <style>   Pak: <accessory>   Rumble: <state>
Stick: +000,+000  C-Stick: +000,+000  L:000 R:000
A:0 B:0 X:0 Y:0  L:0 R:0 Z:0 Start:0
D-U:0 D-D:0 D-L:0 D-R:0  C-U:0 C-D:0 C-L:0 C-R:0
```

- Header line: `Port N` + device **Type** + accessory/pak + rumble (or
  a device-specific status, e.g. GBA boot state).
- State lines: analog row, button rows, etc. — whatever the device
  class has. Held inputs in HELD color against DIM unheld labels;
  connected port headers in ACTIVE.
- **Per-device-class adaptation:** drop fields a given controller
  physically lacks rather than showing dead rows (e.g. N64 pads omit
  the GCN-only C-Stick + X/Y; GCN pads omit the N64-only digital
  C-buttons). A mouse swaps the analog row for absolute + delta. A
  keyboard shows held key names + a typed-text line.
- Center the table against the *actual* surface width, not a
  hard-coded resolution (libraries sometimes hand back a wider buffer).

### 3.4 Idle screensaver

After **~30 seconds** of no input on any port (1800 frames @ 60Hz),
clear to black and bounce the **Joypad logo silhouette sprite**. Each
wall hit advances a **7-color cycle**: red → green → yellow → cyan →
white → orange → hot pink (skip dim blues — they read poorly on CRTs).
Any input wakes it.

- **Use the actual logo sprite, not bfont text.** This is the
  single most-repeated LLM mistake in this repo — instinct says "draw
  some text," the right answer is "blit the silhouette mask in the
  cycle color."
- **Logo pipeline:** `assets/logo.png` → `buildtools/make_logo.py`
  crops to the silhouette bbox and packs it into a console-native
  sprite in a generated `gen_logo.h` next to the code that consumes
  it. Run the packer in the build/CI. The SAME mask is reused on the
  About page but rendered **white** (a static brand mark), never
  cycled.
- "No input" must include *every* input path — pad buttons, analog
  past a deadzone, mouse deltas (no deadzone), and pass-through devices
  like a link-cable controller. A device the main library doesn't
  surface (e.g. a GBA over a link cable) still has to feed the
  idle-reset check.

### 3.5 Options menu

Modal overlay: solid black box, **2-px yellow border**, centered
against the actual surface width.

- **Open:** `Start + D-pad Down` in the controller tester (so Start
  alone stays testable there); **Start alone** in every other mode.
- **Navigate:** D-pad. **A** confirms, **B** cancels.
- **Order:** tester first, accessory/feature modes in the middle,
  **About last**.
- Include the **open-cooldown** trick (a few frames after opening
  before accepting A/B) so a bouncing controller's opening press
  doesn't leak through as a confirm.
- Size the box to its content — don't leave dead space between the last
  row and the footer.

### 3.6 About page

Top-to-bottom, standardized:

1. **Logo sprite, white, centered** at the top (same `gen_logo.h`
   mask the screensaver uses — white here, never cycled).
2. **Title** in TITLE color: `Joypad Tester - <Console>`.
3. **Version** in white: `Version <X.Y.Z>`.
4. **Platform/toolchain credit** in CYAN: e.g. `Built on KallistiOS`.
5. **Detected hardware state** in grey: video / region / refresh /
   cable, etc.
6. **GitHub URL** in grey near the bottom.
7. **Footer hint** in FOOTER color.

### 3.7 Version injection

Single source of truth: `<console>/VERSION`. Inject it at compile time
(`-DJT_VERSION_STR="$(VERSION)"` from the Makefile) so the on-screen
version stays in sync without editing source. Show it on the tester
title row and the About page. Note: a `-D` change alone may not
invalidate cached objects — VERSION bumps want a clean rebuild.

### 3.8 Flicker discipline (single-buffered platforms)

Where the default video mode is single-buffered (e.g. DC): draw
widgets in opaque mode so they self-overpaint; gate heavy redraws on a
"dirty" flag tied to real state changes; vsync *before* drawing; for
moving sprites save-and-restore the underlying pixels. Avoid per-frame
full-buffer clears (beam-race flash). On double-buffered platforms,
blank both buffers at startup so uninitialised VRAM never flashes.

---

## 4. Feature tiers

- **Core (every port):** the controller tester landing screen,
  covering every controller-port device class the console supports.
- **Secondary modes (optional, behind the options menu):** accessory
  editors/browsers, alternate-protocol exercisers, viewers, etc.
  Examples shipped: VMU Icon Editor (dc), Controller Pak Browser /
  GB Camera Viewer / Snap Station Test (n64). **About is always
  present and always last.**

A mode is a function-pointer struct (`jt_mode_t`: name + enter/leave/
update/draw) registered in a table; the dispatcher in `main.c` runs
the current mode and the options menu switches between them. Copy the
`dc/` or `n64/` scaffold.

---

## 5. Detection & parsing contract

- **One row per device class.** Standard pad, alt pad, mouse,
  keyboard, light gun, link-cable device, etc. each render with the
  fields that class has.
- **Identifier-aware labels.** When the device library classifies a
  device as "none/unknown" but the raw identifier tells you what it is
  (a link cable, a voice unit, a keyboard), label it from the
  identifier.
- **Accessory pass-through.** Pak/slot/inline devices render their
  state on the port row (and may add a secondary mode). The underlying
  controller's buttons keep working — an accessory augments the row, it
  doesn't replace it.
- **Fields stay at zero, never blanked.**
- **Named decode over raw hex** when a documented mapping exists
  (transcribe it, cite the source). If the mapping is unknown, show the
  raw value + a note; never invent it.

---

## 6. Per-console device checklist

Before writing code, enumerate the target's controller-port surface.
For each device class, capture *what it feeds the console* and *how to
read it*:

- Standard controller (+ variants / wireless).
- Alternate pads (arcade sticks, wheels, fight pads).
- Mouse / trackball.
- Keyboard (scancode table + protocol).
- Light gun (timing/position protocol).
- Accessories that pass through the port (memory, rumble, transfer,
  bio, microphone, VMU, etc.) — detection + the data each exposes.
- Link cables / inter-console bridges (and any boot/upload protocol,
  e.g. GBA multiboot).
- Multitaps / port expanders.
- Anything specialty (voice units, snap stations, sensors).

For each: the probe/read command, the response byte layout, and the
reverse-engineering or SDK source. Cite sources in the code. This
checklist is the spec for the port's tester rows + which secondary
modes are worth building.

---

## 7. Build & toolchain

- **Pinned Docker toolchain** in `buildtools/Dockerfile`; `build_docker.sh`
  builds the image on first run, then runs `make` in it. Forward
  `--platform=linux/amd64` where the upstream image is amd64-only.
- **`make` writes the artifact into `<console>/build/`.**
- **`build_docker.sh` calls `../collect.sh <console>`** on a successful
  build so the ROM auto-drops into the repo-root gitignored
  `releases/` folder under a stable versionless name
  (`joypad_tester_<console>.<ext>`) — latest build of any console in
  one place. (`collect.sh` also runnable standalone.)
- **`make flash`** (EverDrive/USB-loader-class carts): a host-side
  target that flashes the built ROM over USB via `buildtools/flash.sh`.
  Guard the cross-compile half of the Makefile behind the toolchain
  env var so the file still parses on the host for `flash`.

---

## 8. CI & release

- **`verify-build.yml`:** add a matrix entry (console + image +
  artifacts); gate per-console build steps with
  `if: matrix.console == '<name>'`. Runs on every push/PR.
- **`release.yml`:** tag-driven. Tag `<console>-v<semver>`. The
  workflow verifies `VERSION` matches the tag, builds (gated), stages
  artifacts (gated), extracts the matching CHANGELOG section as the
  release body, and attaches the artifacts.
- **Artifact naming:** `joypad_tester_<console>_v<ver>.<ext>` (console
  before version). Variants get a token (`_wii`, `_toshiba`). Zip large
  padded images (e.g. the DC CDI: ~700MB → ~2MB).
- **Release flow:** bump `VERSION` → prepend CHANGELOG section →
  commit + push → tag `<console>-v<semver>` → push tag. Mismatched
  VERSION fails the release on purpose.

---

## 9. Shared files a new port touches

A port is self-contained except for these:

1. **`README.md`** (repo root): one row in the **Consoles** table
   (`Console | Status | Path | License`, grouped by maturity tier:
   released / beta / alpha / planned, alphabetical within tier) and
   one row in **Acknowledgements**.
2. **`CLAUDE.md`:** an entry in "Existing console subdirs".
3. **`.github/workflows/verify-build.yml`** + **`release.yml`:** the
   matrix/build/stage additions above.

Never enumerate consoles inline in prose anywhere else.

---

## 10. README structure (every console)

Feature-first — written for end users, build details last. The order:

1. **Title** — `# Joypad Tester — <Console>`.
2. **Intro** — one paragraph: what this build does, link back to
   [`../README.md`](README.md).
3. **`## What it does`** — plain-language feature bullets (what each
   device/accessory shows the user), *not* identifier hex or API names.
4. **`## Using it`** — load instructions, controls, the options-menu
   modes.
5. **`## (feature deep-dives)`** *(optional)* — one section per
   non-obvious subsystem; concrete (protocol bytes, timings).
6. **`## For developers`** — Build (Docker), CI link, release flow,
   Origin / credits, LICENSE link.

Keep it tight; no unrelated community shoutouts.

---

## Appendix: hard-won gotchas

- **Screensaver = sprite, not text.** (§3.4)
- **Footer must clear overscan.** Leave ~20px; don't hug the last line.
- **Center against the real surface width**, not a hard-coded res.
- **Anamorphic framebuffers** (N64 320×240 = 2 TV dots/px wide): scale
  sprites 2× horizontally so they aren't squished.
- **VERSION `-D` doesn't invalidate objects** — clean rebuild on bump.
- **Decode tables exist more often than you'd think** — the N64
  RandNet keyboard map was fully documented on the n64brew wiki even
  though the first few searches missed it. Look hard before declaring
  something undocumented or fabricating a mapping.
- **Pass-through accessories augment the row, never replace it** — the
  Bio Sensor must not hide the controller's buttons.
- **Per-port semantics** — rumble follows *that port's* held button,
  not any port's.
