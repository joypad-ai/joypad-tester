# Joypad Tester - Nintendo 64
#
# Builds a single joypad-tester.z64 from src/main.c, linking against
# LibDragon's bundled joypad subsystem (the upstream-merged version of
# meeq/JoypadTest-N64's library). Runs inside the buildtools/ Docker
# container via build_docker.sh.

TARGET    := joypad-tester
SRCDIR    := src
BUILD_DIR := build

# Single source of truth for the version: n64/VERSION. The string is
# injected at compile time as JT_VERSION_STR so src/app.h's fallback
# only kicks in for IDE builds. Same pattern as dc/.
VERSION   := $(shell cat VERSION | tr -d '[:space:]')

SRC := $(SRCDIR)/main.c             \
       $(SRCDIR)/bio.c              \
       $(SRCDIR)/gba.c              \
       $(SRCDIR)/gba_payload.c      \
       $(SRCDIR)/kbd.c              \
       $(SRCDIR)/mouse.c            \
       $(SRCDIR)/screensaver.c      \
       $(SRCDIR)/tpak_info.c        \
       $(SRCDIR)/ui/options_menu.c  \
       $(SRCDIR)/ui/text.c          \
       $(SRCDIR)/modes/tester.c     \
       $(SRCDIR)/modes/cpak.c       \
       $(SRCDIR)/modes/gbc.c        \
       $(SRCDIR)/modes/snap.c       \
       $(SRCDIR)/modes/about.c

OBJS := $(SRC:%.c=$(BUILD_DIR)/%.o)
DEPS := $(SRC:%.c=$(BUILD_DIR)/%.d)

# The cross-compile half of this Makefile only works inside the
# buildtools/ Docker container, where $(N64_INST) points at the
# libdragon toolchain. Guard it so the file still parses on the host
# (which has no n64.mk) -- that lets `make flash` run host-side, since
# the USB loader talks to host hardware, not the build container.
ifneq ($(N64_INST),)

include $(N64_INST)/include/n64.mk

N64_CFLAGS += -DJT_VERSION_STR=\"$(VERSION)\"

all: $(TARGET).z64

$(TARGET).z64: N64_ROM_TITLE = JoypadTester
$(BUILD_DIR)/$(TARGET).elf: $(OBJS)

# n64.mk writes the .z64 next to the Makefile; build_docker.sh
# mirrors it into build/ post-make so other consoles' artifact paths
# stay consistent. We deliberately do NOT add a $(BUILD_DIR)/%.z64
# target here -- it would match n64.mk's %.z64 pattern and cause its
# `LDFLAGS+=$(N64_LDFLAGS)` target-specific assignment to fire twice
# (once for the copy target, once for its prerequisite), which puts
# `-Tn64.ld` on the link command line twice and ld blows up with
# "linker script file 'n64.ld' appears multiple times".

clean:
	rm -rf $(BUILD_DIR) $(TARGET).z64

-include $(DEPS)

else   # host (no N64 toolchain): build is delegated to Docker

all:
	@echo "Build with ./build_docker.sh (needs the libdragon toolchain)."

clean:
	rm -rf $(BUILD_DIR) $(TARGET).z64

endif

# Flash the built ROM to an EverDrive/64drive/SC64 over USB via
# UNFLoader. Host-side; run AFTER ./build_docker.sh, with the cart
# sitting at the EverDrive menu. Shared helper so other EverDrive
# consoles can adopt the same `make flash` pattern.
flash:
	@buildtools/flash.sh $(BUILD_DIR)/$(TARGET).z64

.PHONY: all clean flash
