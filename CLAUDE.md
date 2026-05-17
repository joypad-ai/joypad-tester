# CLAUDE.md — joypad-tester

This repo holds one test app per console, each in its own subdirectory.
The patterns below describe how each console subdir is structured so
that new consoles can be added without churning the top-level scaffolding
or the CI/release wiring.

## Repository shape

```
.
├── CLAUDE.md            # this file
├── LICENSE.md           # MIT, covers repo scaffolding + any future common/
├── README.md            # top-level overview
├── .github/
│   ├── FUNDING.yml      # Sponsor button → RobertDaleSmith
│   └── workflows/
│       ├── verify-build.yml   # matrix CI for every push/PR
│       └── release.yml        # tag-driven per-console releases
├── <console>/           # one subdir per supported console
│   ├── VERSION          # bare semver string, e.g. "0.1.0"
│   ├── CHANGELOG.md     # console-scoped, header style: "## v0.1.0 — YYYY-MM-DD"
│   ├── LICENSE.md       # whatever the upstream code's licence is
│   ├── README.md        # console-scoped overview
│   ├── Makefile         # console-specific build
│   ├── source/ (or ppc/, etc.)
│   ├── build*/          # build outputs; intermediate files gitignored,
│   │                    # final artifacts (.dol/.gba/_payload.c) committed
│   │                    # if downstream consumers need them prebuilt
│   └── ...
└── common/              # (future) cross-console shared helpers
```

Subdir names are short 3-letter codenames matching homebrew-community
usage (`gcn`, `gba`, `pce`, future `n64` / `snes` / …). The same
codename is used as the release-tag prefix (`<codename>-v<semver>`).
Don't introduce full-localized-name subdirs (`gamecube/`,
`gameboyadvance/`, etc.) — the codename is the source of truth. Also
avoid 2-letter codenames (`gc`) since they tend to collide with
unrelated paths in vendored / sibling repos (joypad-os has its own
`gc/` subdir for the GameCube host that has nothing to do with our
subdir codename).

Existing console subdirs at time of writing:

- `gcn/` — GameCube + Wii test app (libogc2-based, builds .dol; also
  surfaces N64 controllers via the passive adapter on GC ports).
  Origin: zlib (corenting GC-Controller-Test); `gcn/LICENSE.md`.
- `gba/` — GBA multiboot payload, two variants from one source tree
  (eyes for joypad-os consumers, tester for the GameCube host or
  flashcart use). Origin: MIT (Doridian Joybus-PIO); `gba/LICENSE.md`.
- `pce/` — PC Engine / TurboGrafx-16 test app (HuC, builds .pce).
  Detects 2-button / 6-button pads and the PCE mouse via the standard
  joypad port + multitap. Origin: MIT (dshadoff PCE_Mouse_Test);
  `pce/LICENSE.md`.
- `3do/` — 3DO Opera test app (trapexit/3do-devkit-based, builds
  iso9660 .iso). Reads the daisy-chain control pad and renders live
  button state. Origin: MIT (original src/main.cpp) + ISC for the
  vendored devkit helpers; `3do/LICENSE.md`. Toolchain is Linux-only
  x86 binaries running under `--platform=linux/amd64` in the build
  container, so Docker is mandatory for this subdir.
- `dc/` — Sega Dreamcast test app (KallistiOS-based, builds selfboot
  `.cdi`). Tests every maple-bus device class — controllers, mice,
  keyboards, light guns — plus per-port expansion slot peripherals
  (VMU, Purupuru, microphone). Hosts a built-in VMU icon editor mode
  (v0.2+, ported from `RobertDaleSmith/vmu-icon-maker`). First subdir
  to ship the **mode-switching scaffold** (Start+Down opens a modal
  options menu; modes share input + per-port state; new modes are a
  function-pointer struct in `dc/src/modes/`). Future testers can
  adopt this scaffold to add extended capabilities beyond bare
  controller readouts. Origin: MIT (original `dc/src/`) over
  KallistiOS (BSD-style, statically linked); `dc/LICENSE.md`.
  Toolchain (KOS + dc-chain sh4-elf gcc) is built from source inside
  `dc/buildtools/Dockerfile` (~10-15 min first build, layer-cached
  after).

## Console subdir conventions

Each console subdir is a self-contained product. Adding a new one
should not require touching anything outside its own directory, with
the exception of the two CI workflows and CLAUDE.md's "Existing console
subdirs" list.

### Files every console subdir must have

| File          | Purpose                                                       |
|---------------|---------------------------------------------------------------|
| `VERSION`     | Bare semver string. Must match the release tag (see below).   |
| `CHANGELOG.md`| Per-version release notes — the canonical source of truth for what shipped. One section per release, header `## v<semver> — <date>`, body = 1-paragraph summary + a small Highlights list. The GitHub Release body is a one-line pointer to this file (GitHub's auto-rendered Assets list handles the file list, no need to repeat it in either the changelog or the release body). |
| `LICENSE.md`  | Whatever the upstream code's licence is (zlib, MIT, …).        |
| `README.md`   | Audience-facing overview: what the app is, how to build, how to embed. The long-form feature breakdown lives here, not in the changelog. |
| `Makefile`    | Build entrypoint. Use a Docker-based toolchain if it eases CI. |

### README structure (every console subdir)

Each `<console>/README.md` follows the same top-to-bottom outline so
they're navigable as a set. Sections marked *(optional)* are included
only when the console has that surface; everything else is required.

1. **Title** — `# Joypad Tester — <Console>`
2. **Intro** — one paragraph: what this build does and a link back to
   the top-level repo (`[Joypad Tester](../README.md)`).
3. **`## What it tests`** — concrete ASCII example of the on-screen
   output, then a table covering the per-port / per-variant matrix
   (controller types, payload variants, etc.). Always state which
   fields are unavailable on this platform so reading "zeros" isn't
   ambiguous.
4. **`## (feature deep-dives)`** — one `##` section per non-obvious
   subsystem (accessory paks, alt protocols, BIOS quirks, idle modes,
   etc.). Use as many as needed. Keep them concrete: protocol bytes,
   timings, register addresses where relevant.
5. **`## Build`** — preferred toolchain commands (devkitPro / devkitARM
   / etc.) followed by the Docker fallback (`./build_docker.sh` or a
   bare `docker run` line). End with a link to
   `.github/workflows/verify-build.yml`.
6. **`## Loading on hardware`** — how the artifact gets onto real
   hardware: SD-card layout, multiboot upload, flash cart slot, etc.
   Include the canonical loader (Swiss, joypad-os, etc.) where it
   applies.
7. **`## Embedding`** *(optional)* — only when the subdir produces a
   payload another product consumes. State the symbol names and the
   reference uploader file.
8. **`## Releases`** — one paragraph: tag format (`<console>-v<semver>`),
   link to `CHANGELOG.md`, list of artifacts the release workflow
   attaches.
9. **`## Origin / credits`** — upstream lineage (project + license +
   link), what this subtree adds, and a link to `LICENSE.md`.

`gcn/README.md` is the canonical example. When adding a new console,
copy its skeleton and fill each section in — don't reorder or rename
headings, because the top-level README and CLAUDE.md both reference
them.

### Build outputs

Intermediates (`*.o`, `*.d`, `*.elf`, `*.map`) should be `.gitignore`d.
Final artifacts (`*.dol`, `*.gba`, `*_payload.c`) get committed **only
when downstream consumers need them prebuilt** (e.g., joypad-os
submodules `gba/` and consumes `build/joypad/joypad_payload.c` without
running devkitARM). For consoles whose artifacts are end-user
downloads only, leave them out of git and let the release workflow
build them.

## CI: build verification (`.github/workflows/verify-build.yml`)

Runs on every push to `main` and every PR. Strategy is a matrix:

```yaml
matrix:
  include:
    - console: <name>
      image:   <docker image with the toolchain>
      artifacts: |
        <path/to/output1>
        <path/to/output2>
```

Per-console steps are gated with `if: matrix.console == '<name>'` so a
matrix entry only runs the build commands it needs. Adding a console
means adding a matrix entry + the corresponding build step(s).

## CI: releases (`.github/workflows/release.yml`)

Tag-driven. Tag format: `<console>-v<semver>` (e.g. `gcn-v1.0.0`,
`gba-v1.0.0`, `pce-v1.0.0`). Pre-release suffixes allowed (`-alpha.1`,
`-rc.2`, …). All three consoles joint-released as v1.0.0 on
2026-05-11 once the naming convention (short 3-letter codenames),
artifact-naming pattern (`joypad_tester_v<ver>.<ext>`), and inter-
console wiring (gcn embeds gba's tester ROM) settled. Pre-1.0
internal iterations (gamecube-v0.1.0, gc-v0.2.0, individual v0.1.0
cuts of the other consoles) were nuked before going public.

The workflow:

1. Parses the tag to extract `<console>` and `<semver>`.
2. Verifies `<console>/VERSION` matches the tag's version. (Mismatch
   fails the build — there's no auto-bump.)
3. Builds the requested console (gated by `if:` on each build step,
   same pattern as verify-build).
4. Stages release artifacts into `<console>/_release/` (also gated).
5. Extracts the relevant section of `<console>/CHANGELOG.md` as the
   release body (`## v<semver> — <date>` to the next `## ` header).
6. Creates a GitHub Release with the artifacts attached.

Adding a console = adding a build step + a stage-artifacts step (both
gated on `if: needs.parse.outputs.console == '<name>'`), and a case for
the pretty name in the `parse` job.

## Release flow for a console

To cut, e.g., `gba-v1.1.0`:

1. Bump `<console>/VERSION` to `1.1.0`.
2. Prepend a section to `<console>/CHANGELOG.md`:
   `## v1.1.0 — YYYY-MM-DD`, 1-paragraph summary, a small Highlights
   list. No Artifacts section -- GitHub's auto-rendered Assets list
   on the release page already lists every attached file, and the
   release body itself is just a link to this file.
3. Commit + push to `main`.
4. Tag `<console>-v1.1.0` and push the tag.
5. CI parses the tag, verifies VERSION, builds, attaches the
   artifacts, and writes a one-line release body pointing at
   `<console>/CHANGELOG.md`.

If `VERSION` doesn't match the tag, the release fails (intentional —
forces the changelog/version-bump commits to land before the tag).

## Top-level README

The repo-root `README.md` stays a thin index — its only per-console
state lives in two tables:

- **Consoles** — `Console | Status | Path | License`. The License
  column links to `<console>/LICENSE.md` so the bottom-of-page License
  section never has to enumerate per-console licenses.
- **Acknowledgements** — `Console | Origin / inspiration`. One row per
  console summarising upstream lineage; deep credits live in each
  console's `README.md` "Origin / credits" section.

Adding a console = adding one row to each table. Never enumerate
consoles inline in prose anywhere else in the top-level README — it
breaks at N consoles.

## When extending or refactoring

- Top-level scaffolding (workflows, CLAUDE.md, repo-wide README, top-
  level LICENSE.md) is shared and should stay generic. If a new
  console needs something fundamentally different that doesn't fit
  the conventions above, prefer extending the conventions (and
  documenting here) over per-console drift.
- A `common/` dir for cross-console helpers is reserved and should
  inherit the top-level MIT licence.
- Submodule consumers (joypad-os pulls in `gba/`) rely on the
  committed prebuilt artifacts in `build/`. Don't break that path
  without coordinating the consumer side.
