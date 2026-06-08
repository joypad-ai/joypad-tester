# joypad-tester OG Xbox build wrapper around nxdk.
#
# Run inside the upstream nxdk Docker image (ghcr.io/xboxdev/nxdk),
# which provides clang, lld, cxbe, extract-xiso, and the prebuilt
# nxdk at /usr/src/nxdk. build_docker.sh handles the docker run.
#
# Outputs in build/:
#   default.xbe           -- the bootable Xbox executable.
#   joypad_tester_xbox_v$(VERSION).iso
#                         -- bootable ISO that wraps the same XBE,
#                            for DVD-R or softmod loaders.
#
# Pattern (XBE_TITLE, GEN_XISO, SRCS, then include nxdk's Makefile)
# matches nxdk's own samples/hello layout.

VERSION    := $(shell cat VERSION | tr -d '[:space:]')
XBE_TITLE   = Joypad\ Tester
GEN_XISO    = build/joypad_tester_xbox_v$(VERSION).iso
OUTPUT_DIR := $(CURDIR)/build

SRCS := $(CURDIR)/src/main.c \
        $(CURDIR)/src/ports/ports.c \
        $(CURDIR)/src/modes/tester.c

NXDK_DIR ?= /usr/src/nxdk

# Pull in SDL2 + pbkit. nxdk's Makefile fragments expose these as
# NXDK_SDL=y / NXDK_CXX=y feature flags; pbkit is unconditional. The
# sdl_gamecontroller sample does the same.
NXDK_SDL = y

include $(NXDK_DIR)/Makefile
