# joypad-tester OG Xbox build wrapper around nxdk.
#
# Run inside the upstream nxdk Docker image (ghcr.io/xboxdev/nxdk),
# which provides clang, lld, cxbe, extract-xiso, and the prebuilt
# nxdk at /usr/src/nxdk. build_docker.sh handles the docker run.
#
# Outputs in build/:
#   default.xbe           -- the bootable Xbox executable.
#   joypad_tester_xbox_v$(VERSION).iso
#                         -- bootable XISO that wraps the same XBE.
#
# Pattern (XBE_TITLE, GEN_XISO, SRCS, then include nxdk's Makefile)
# matches nxdk's own samples/hello layout.

VERSION    := $(shell cat VERSION | tr -d '[:space:]')
XBE_TITLE   = Joypad\ Tester
GEN_XISO    = build/joypad_tester_xbox_v$(VERSION).iso
OUTPUT_DIR := $(CURDIR)/build

SRCS := $(CURDIR)/src/main.c \
        $(CURDIR)/src/ports/ports.c \
        $(CURDIR)/src/ports/accessories.c \
        $(CURDIR)/src/ui/bfont.c \
        $(CURDIR)/src/ui/options_menu.c \
        $(CURDIR)/src/ui/screensaver.c \
        $(CURDIR)/src/modes/tester.c \
        $(CURDIR)/src/modes/about.c

NXDK_DIR ?= /usr/src/nxdk

# Pull in SDL2 from nxdk.
NXDK_SDL = y

# Pull in nxdk's USB Mass Storage + Audio + HID class drivers.
# Xbox MUs are USB MSC (xboxdevwiki.net/Xbox_Memory_Unit); the
# Voice Communicator / headset are USB UAC; keyboards + mice are
# standard boot-protocol HID. Without these flags nxdk's
# lib/usb/Makefile skips the class drivers and links fail on the
# usbh_umas_init / usbh_uac_init / usbh_hid_init entry points.
NXDK_USB_ENABLE_MSC = y
NXDK_USB_ENABLE_UAC = y
NXDK_USB_ENABLE_HID = y

# PORTING.md §3.7: single source of truth is xbox/VERSION; inject
# the string at compile time so the on-screen title row and About
# page stay in sync without editing source.
#
# Build hash too -- short git SHA + "-dirty" suffix when the working
# tree is unclean -- so users can confirm the binary they FTP'd is
# the latest one from `make` (otherwise a "version=0.1.0" title is
# ambiguous between iterations of the same VERSION).
#
# When the build runs inside Docker, the container mounts only xbox/
# so an in-container `git rev-parse` can't find .git (it lives one
# level up at the repo root). build_docker.sh computes the SHA on
# the host and exports it via env (HOST_BUILD_SHA / HOST_BUILD_DIRTY);
# we prefer that, then fall back to in-tree git for native builds.
BUILD_SHA   := $(if $(HOST_BUILD_SHA),$(HOST_BUILD_SHA),$(shell git rev-parse --short=7 HEAD 2>/dev/null || echo nogit))
BUILD_DIRTY := $(if $(filter undefined,$(origin HOST_BUILD_DIRTY)),$(shell git diff --quiet 2>/dev/null || echo -dirty),$(HOST_BUILD_DIRTY))
BUILD_STAMP := $(BUILD_SHA)$(BUILD_DIRTY)
CFLAGS += -DJT_VERSION_STR=\"$(VERSION)\"
CFLAGS += -DJT_BUILD_STAMP=\"$(BUILD_STAMP)\"

include $(NXDK_DIR)/Makefile
