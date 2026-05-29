# Joypad Tester — Dreamcast
#
# Builds a selfboot CDI (joypad-tester-dreamcast.cdi) using KallistiOS.
# Uses the canonical `kos-cc` build wrapper that KOS environ.sh adds to
# PATH — it resolves to sh-elf-gcc with KOS include paths, link flags,
# crt0, and the kallisti libs already wired up. See KOS examples like
# examples/dreamcast/hello/Makefile for the upstream pattern.
#
# Expects $KOS_BASE to point at a KOS install and `mkdcdisc` + `kos-cc`
# on PATH. The buildtools/ Docker image sources environ.sh as part of
# its entrypoint, so plain `make` works inside the container. On a
# Linux host with KOS installed, source $KOS_BASE/environ.sh first.

BUILDDIR := build
SRCDIR   := src

# Single source of truth for the version: dc/VERSION. The string gets
# injected at compile time so app.h's JT_VERSION_STR macro stays in
# sync without us touching the source.
VERSION := $(shell cat VERSION | tr -d '[:space:]')

# Output target name matches the release artifact shape
# (joypad_tester_dc_v<semver>.cdi) so the locally built CDI, the
# collect.sh artifact, and the GH Actions release all line up.
TARGET   := joypad_tester_dc_v$(VERSION)

# Source layout. Keep modes/* / input/* / ports/* / ui/* / video/*
# as discrete translation units so future testers can adopt the
# scaffold by copying matching subdirs.
SOURCES := \
    $(SRCDIR)/main.c \
    $(SRCDIR)/video/mode.c \
    $(SRCDIR)/input/cursor.c \
    $(SRCDIR)/ports/ports.c \
    $(SRCDIR)/ui/bfont_util.c \
    $(SRCDIR)/ui/options_menu.c \
    $(SRCDIR)/ui/osk.c \
    $(SRCDIR)/ui/screensaver.c \
    $(SRCDIR)/vms/vms.c \
    $(SRCDIR)/vms/apply.c \
    $(SRCDIR)/library/library.c \
    $(SRCDIR)/canvas/canvas.c \
    $(SRCDIR)/modes/tester.c \
    $(SRCDIR)/modes/vmu_editor.c \
    $(SRCDIR)/modes/browser.c \
    $(SRCDIR)/modes/lib_browser.c \
    $(SRCDIR)/modes/about.c

OBJECTS := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SOURCES))

CFLAGS  += -Wall -Wextra -Wno-unused-parameter -I$(SRCDIR) \
           -DJT_VERSION_STR=\"$(VERSION)\"

ELF := $(BUILDDIR)/$(TARGET).elf
CDI := $(BUILDDIR)/$(TARGET).cdi

# Generated assets. The library save's BIOS icon is baked from an
# ICONDATA_VMS exported by the dcvmuicons.net web tool -- drop a new
# folder under assets/library_icon/ and `make` will refresh the header.
LIBRARY_ICON_SRC := assets/library_icon/ICONDATA.VMS
LIBRARY_ICON_HDR := $(SRCDIR)/library/gen_library_icon.h
LIBRARY_ICON_GEN := buildtools/make_library_icon.py

FALLBACK_ICON_SRC := assets/fallback_vmu_icon/ICONDATA.VMS
FALLBACK_ICON_HDR := $(SRCDIR)/library/gen_fallback_vmu_icon.h
FALLBACK_ICON_GEN := buildtools/make_fallback_vmu_icon.py

BIOS_ICONS_SRC := assets/bios_icons/BIOSDUMP.VMS
BIOS_ICONS_HDR := $(SRCDIR)/library/gen_bios_icons.h
BIOS_ICONS_GEN := buildtools/make_bios_icons.py

.PHONY: all clean

all: $(CDI)

$(LIBRARY_ICON_HDR): $(LIBRARY_ICON_SRC) $(LIBRARY_ICON_GEN)
	python3 $(LIBRARY_ICON_GEN) $(LIBRARY_ICON_SRC) $@

$(FALLBACK_ICON_HDR): $(FALLBACK_ICON_SRC) $(FALLBACK_ICON_GEN)
	python3 $(FALLBACK_ICON_GEN) $(FALLBACK_ICON_SRC) $@

$(BIOS_ICONS_HDR): $(BIOS_ICONS_SRC) $(BIOS_ICONS_GEN)
	python3 $(BIOS_ICONS_GEN) $(BIOS_ICONS_SRC) $@

# library.c #includes the generated header; make the dep explicit so a
# refreshed ICONDATA propagates without needing `make clean`.
$(BUILDDIR)/library/library.o: $(LIBRARY_ICON_HDR)

# browser.c #includes the fallback + bundled preset-icon headers.
$(BUILDDIR)/modes/browser.o: $(FALLBACK_ICON_HDR) $(BIOS_ICONS_HDR)

# kos-cc is the KOS build wrapper; it bakes in KOS_CFLAGS / KOS_CPPFLAGS
# / include paths / -lkallisti -lm -lc -lgcc on the link step. We do
# *not* include $(KOS_BASE)/Makefile.rules — its default %.o: %.c rule
# would conflict with our build/-prefixed object paths.
$(BUILDDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	kos-cc $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(ELF): $(OBJECTS)
	@mkdir -p $(BUILDDIR)
	kos-cc -o $@ $(OBJECTS)

# Custom license-screen ("boot") logo embedded into IP.BIN. MR-format
# image (320x90, black-on-white joypad logo) generated offline by
# buildtools/make_mr_logo.py and committed under assets/. mkdcdisc copies
# it verbatim into IP.BIN (must stay <= 8192 bytes).
BOOT_LOGO := assets/boot_logo/joypad_boot.mr

# mkdcdisc subsumes scramble + IP.BIN + iso9660 + CDI conversion in
# one call. Default IP.BIN already advertises VGA-cable compatibility
# (peripheral mask 0xE000F10 has the 0x10 VGA bit set), so VGA boxes
# accept the disc without extra flags.
$(CDI): $(ELF) $(BOOT_LOGO)
	mkdcdisc -e $(ELF) -o $(CDI) -n "JOYPAD TESTER" \
	    -a "Robert Dale Smith" -r 20260516 \
	    -i $(BOOT_LOGO)
	@echo "Built $(CDI)"

clean:
	rm -rf $(BUILDDIR)
