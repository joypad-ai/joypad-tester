# Joypad Tester - Nintendo 64
#
# Builds a single joypad-tester.z64 from src/main.c, linking against
# LibDragon's bundled joypad subsystem (the upstream-merged version of
# meeq/JoypadTest-N64's library). Runs inside the buildtools/ Docker
# container via build_docker.sh.

TARGET    := joypad-tester
SRCDIR    := src
BUILD_DIR := build

SRC := $(SRCDIR)/main.c     \
       $(SRCDIR)/gba.c      \
       $(SRCDIR)/gba_payload.c

OBJS := $(SRC:%.c=$(BUILD_DIR)/%.o)
DEPS := $(SRC:%.c=$(BUILD_DIR)/%.d)

include $(N64_INST)/include/n64.mk

all: $(BUILD_DIR)/$(TARGET).z64

$(TARGET).z64: N64_ROM_TITLE = JoypadTester
$(BUILD_DIR)/$(TARGET).elf: $(OBJS)

# n64.mk writes the .z64 next to the Makefile; copy it into build/ so
# the artifact layout matches the other consoles (gcn/, pce/, etc.).
$(BUILD_DIR)/$(TARGET).z64: $(TARGET).z64
	cp $< $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET).z64

-include $(DEPS)

.PHONY: all clean
