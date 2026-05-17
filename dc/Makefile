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

TARGET   := joypad-tester-dreamcast
BUILDDIR := build
SRCDIR   := src

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
    $(SRCDIR)/vms/vms.c \
    $(SRCDIR)/vms/apply.c \
    $(SRCDIR)/library/library.c \
    $(SRCDIR)/canvas/canvas.c \
    $(SRCDIR)/modes/tester.c \
    $(SRCDIR)/modes/vmu_editor.c \
    $(SRCDIR)/modes/browser.c \
    $(SRCDIR)/modes/about.c

OBJECTS := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.o,$(SOURCES))

CFLAGS  += -Wall -Wextra -Wno-unused-parameter -I$(SRCDIR)

ELF := $(BUILDDIR)/$(TARGET).elf
CDI := $(BUILDDIR)/$(TARGET).cdi

.PHONY: all clean

all: $(CDI)

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

# mkdcdisc subsumes scramble + IP.BIN + iso9660 + CDI conversion in
# one call. Default IP.BIN already advertises VGA-cable compatibility
# (peripheral mask 0xE000F10 has the 0x10 VGA bit set), so VGA boxes
# accept the disc without extra flags.
$(CDI): $(ELF)
	mkdcdisc -e $(ELF) -o $(CDI) -n "JOYPAD TESTER" \
	    -a "Robert Dale Smith" -r 20260516
	@echo "Built $(CDI)"

clean:
	rm -rf $(BUILDDIR)
