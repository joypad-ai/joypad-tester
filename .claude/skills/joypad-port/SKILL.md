---
name: joypad-port
version: 1.0.0
description: >-
  Scaffold a new Joypad Tester console port to the repo's standard. Fire
  this for requests like "build Joypad Tester for <console>", "add a
  <console> port", "new joypad tester for <console>", or "port the
  joypad tester to <console>". Researches the console's controller-port
  protocol + homebrew toolchain, scaffolds the subdir to PORTING.md spec
  (mode scaffold, screensaver, options menu, About, CI + docs wiring),
  and emits a per-console device/parsing checklist for the human to
  fill in the console-specific reads. Guided scaffold — it does NOT
  blind-write protocol code it can't verify.
---

# Build a Joypad Tester port

You are adding a new console to the multi-console Joypad Tester repo.
Produce a port that feels like it belongs in the set: same visual
system, same feature shape, same parsing philosophy.

**Read [`PORTING.md`](../../../PORTING.md) in full first.** It is the
authoritative spec; everything below is the procedure for applying it.
Cross-reference `dc/` and `n64/` as the reference implementations
(dc = single-buffered + an editor mode; n64 = mode scaffold + many
pass-through devices).

## Step 1 — Lock the target

- Confirm the console and pick its **3-letter lowercase codename**
  (matching homebrew-community usage; not 2-letter, not a full name).
  If ambiguous, ask.
- Check it isn't already a subdir. Note its maturity tier
  (released / beta / alpha / planned) for the README table.

## Step 2 — Research (this is the real work)

Build the **per-console device checklist** (PORTING.md §6). Use web
search + look for prior art (existing homebrew test ROMs, SDK docs,
n64brew/console-protocol wikis, reverse-engineering write-ups). For
the target, determine:

- The homebrew **SDK / toolchain** and whether a pinned **Docker
  image** exists (prefer Docker so CI matches local).
- Every **controller-port device class**: standard pad (+ variants),
  alt pads, mouse, keyboard, light gun, pass-through accessories
  (memory / rumble / transfer / bio / mic / etc.), link cables /
  bridges (+ any boot/upload protocol), multitaps, specialty devices.
- For each device: what it feeds the console, the probe/read command,
  and the response byte layout — **with sources**. Where a decode
  table (e.g. a keyboard scancode map) exists, find and transcribe it;
  do not fabricate one. Look hard before declaring something
  undocumented (the RandNet keyboard map was on the wiki even though
  early searches missed it).

Write this checklist into the port's `README.md` deep-dive section and
leave it as the TODO list for console-specific parsing.

## Step 3 — Scaffold the subdir to spec

Create `<codename>/` with everything in PORTING.md §1–§3, copying the
structure (not blindly the code) from `dc/` or `n64/`:

- `VERSION` (`0.1.0`), `CHANGELOG.md`, `LICENSE.md` (upstream licence),
  `README.md` (feature-first skeleton, §10), `Makefile` +
  `build_docker.sh` + `buildtools/Dockerfile` (pinned toolchain).
- `assets/logo.png` (copy the repo's logo source) +
  `buildtools/make_logo.py` adapted to the console's native sprite
  format → generated `gen_logo.h`.
- `src/` mode scaffold: `app.h` (`jt_mode_t` contract + mode enum),
  `main.c` (dispatcher + idle/screensaver tick + options-menu
  integration), `ui/text.*` (the `JT_COL_*` palette + draw helpers),
  `ui/options_menu.*`, `ui/screensaver.*`, `modes/tester.*` (landing),
  `modes/about.*` (last).
- Wire **version injection** (`-DJT_VERSION_STR` from VERSION).
- Implement the **visual system** (§3) fully — title, footer,
  per-port readout shell, screensaver, options menu, About. These are
  console-agnostic; get them pixel-correct now.
- For the **device reads** (§5), implement what you can verify from
  Step 2 and leave clearly-marked `TODO(<codename>):` stubs for the
  rest, each pointing at the checklist entry + its source. **Do not
  ship guessed protocol code as if it were verified** — stub it and
  flag it.

## Step 4 — Wire the shared files (PORTING.md §9)

- Repo-root `README.md`: a **Consoles** row (right maturity tier,
  alphabetical within tier) + an **Acknowledgements** row.
- `CLAUDE.md`: an "Existing console subdirs" entry.
- `.github/workflows/verify-build.yml` + `release.yml`: matrix entry +
  gated build/stage steps; artifact name
  `joypad_tester_<codename>_v<ver>.<ext>`.
- If the cart has a USB loader (EverDrive-class), add a `make flash`
  target + a `buildtools/flash.sh` case.

## Step 5 — Verify + hand off

- Build via `./build_docker.sh` if the toolchain is available; fix
  compile errors. If the toolchain can't run here, say so.
- Run `./collect.sh <codename>` to confirm the artifact lands in
  `releases/`.
- Summarize: what's fully implemented (the visual system + verified
  device reads) vs the `TODO(<codename>):` checklist of
  console-specific reads the human needs to finish/verify on hardware.

## Conventions to honor (do not redesign)

- Controller tester is the landing screen; deep features behind the
  options menu (VMU Icon Editor is the template).
- All ports live simultaneously; fields stay at zero, never blanked.
- Screensaver = bouncing logo **sprite** in the 7-color cycle, not
  text.
- Title = TITLE/yellow centered (+ version); footer = FOOTER/green
  centered, overscan-safe; About last in the menu.
- Decode to human-readable when a documented mapping exists; raw +
  flag when it doesn't; never fabricate.
- Pass-through accessories augment the port row, never replace it.
- No `Co-Authored-By` trailers on commits.

When in doubt, match `dc/` or `n64/` and PORTING.md — not training-data
instinct.
