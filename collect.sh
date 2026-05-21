#!/usr/bin/env bash
#
# collect.sh — gather locally-built ROMs into one gitignored
# releases/ folder, named with the commit they were built from so you
# can tell builds apart regardless of which subdir you're working in.
#
#   ./collect.sh             # collect every console that has a build
#   ./collect.sh n64 dc      # collect only the named console(s)
#
# Each console's build_docker.sh calls `./collect.sh <console>` at the
# end of a successful build, so artifacts land here automatically; run
# it by hand any time to re-gather everything currently built.
#
# Names embed the short commit hash (joypad-os style):
#   joypad_tester_<console>_<shorthash>.<ext>
# plus a "-dirty" suffix when that console's tree has uncommitted
# changes (we build untagged test ROMs a lot, so the bare HEAD hash
# alone would be misleading). One file per commit accumulates here;
# `ls -t releases/` shows the newest. The release workflow attaches the
# versioned names -- this is a dev convenience only.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="$ROOT/releases"
mkdir -p "$DEST"

want=("$@")
selected() {
    [ ${#want[@]} -eq 0 ] && return 0
    for w in "${want[@]}"; do [ "$w" = "$1" ] && return 0; done
    return 1
}

# rev <console-subdir> -> short commit hash, with -dirty if that subdir
# has uncommitted changes.
rev() {
    local h
    h=$(git -C "$ROOT" rev-parse --short=7 HEAD 2>/dev/null || echo unknown)
    if [ -n "$(git -C "$ROOT" status --porcelain -- "$ROOT/$1" 2>/dev/null)" ]; then
        h="${h}-dirty"
    fi
    printf '%s' "$h"
}

# copy <dest-name> <src-candidate>...  -- copies the first existing
# candidate to releases/<dest-name>. Missing sources are skipped
# silently (a console simply hasn't been built yet).
copy() {
    local name="$1"; shift
    local src
    for src in "$@"; do
        if [ -f "$src" ]; then
            cp -f "$src" "$DEST/$name"
            echo "  releases/$name  <-  ${src#"$ROOT"/}"
            return 0
        fi
    done
    return 0
}

if selected gcn; then
    H=$(rev gcn)
    copy "joypad_tester_gcn_${H}.dol" "$ROOT"/gcn/joypad-tester-gamecube.dol "$ROOT"/gcn/build*/*gamecube*.dol
    copy "joypad_tester_wii_${H}.dol" "$ROOT"/gcn/joypad-tester-wii.dol      "$ROOT"/gcn/build*/*wii*.dol
fi
if selected gba; then
    H=$(rev gba); copy "joypad_tester_gba_${H}.gba" "$ROOT"/gba/build/tester/tester_mb.gba
fi
if selected pce; then
    H=$(rev pce); copy "joypad_tester_pce_${H}.pce" "$ROOT"/pce/build/joypad-tester.pce
fi
if selected 3do; then
    H=$(rev 3do); copy "joypad_tester_3do_${H}.iso" "$ROOT"/3do/build/joypad-tester.iso
fi
if selected dc; then
    H=$(rev dc); copy "joypad_tester_dc_${H}.cdi" "$ROOT"/dc/build/joypad-tester-dreamcast.cdi
fi
if selected n64; then
    H=$(rev n64); copy "joypad_tester_n64_${H}.z64" "$ROOT"/n64/build/joypad-tester.z64
fi
if selected nuon; then
    H=$(rev nuon)
    copy "joypad_tester_nuon_${H}.iso" "$ROOT"/nuon/build/joypad-tester.iso
    copy "joypad_tester_nuon_${H}.run" "$ROOT"/nuon/build/nuon.run
fi

echo "Collected into releases/"
