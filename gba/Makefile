# joypad-gba — GBA multiboot payload that doubles as a USB controller
# (via the GC↔GBA link cable) and renders animated cartoon eyes on the
# GBA screen.
#
# The joybus handshake + per-VBlank JOYTR write (in source/main.c) is
# Doridian's gba-as-controller reference (github.com/Doridian/Joybus-PIO)
# verbatim — the cable's level-shifter MCU only stays happy with that
# exact sequence. The eyes overlay runs in the VBlank slot on top.
#
# TARGET uses the "_mb" suffix so devkitARM's stock %_mb.elf rule picks
# the multiboot specs file (loads at 0x02000000 instead of 0x08000000).

ifeq ($(strip $(DEVKITPRO)),)
$(error "Set DEVKITPRO. typically: export DEVKITPRO=/opt/devkitpro")
endif

include $(DEVKITPRO)/devkitARM/gba_rules

# devkitPro's stock rules invoke `$(LD)` for the link step but never
# define LD anywhere — make's default is plain `ld`, which doesn't
# understand `-specs=` or `-mthumb-interwork`. Force LD to the gcc
# frontend so these flags pass through.
export LD := $(CC)

TARGET   := joypad
BUILD    := build
SOURCES  := source
INCLUDES :=

ARCH     := -mthumb-interwork -mthumb
CFLAGS   := -g -Wall -O3 \
            -mcpu=arm7tdmi -mtune=arm7tdmi \
            -fomit-frame-pointer -ffast-math \
            $(ARCH) $(INCLUDE)
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions
ASFLAGS  := $(ARCH)
LDFLAGS  := -g $(ARCH) -Wl,-Map,$(TARGET).map

LIBS     := -lgba -lm
LIBDIRS  := $(LIBGBA)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT  := $(CURDIR)/$(BUILD)/$(TARGET)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
SFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))

export OFILES   := $(addsuffix .o,$(BINFILES)) $(CFILES:.c=.o) $(SFILES:.s=.o)
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: all clean

all: $(BUILD)
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

$(BUILD):
	@mkdir -p $@

clean:
	@echo cleaning ...
	@rm -rf $(BUILD)/*.o $(BUILD)/*.d $(BUILD)/*.elf $(BUILD)/*.map $(TARGET).map

else

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT)_payload.c

$(OUTPUT)_payload.c: $(OUTPUT)_mb.gba
	@echo embedding $(notdir $<) -^> $(notdir $@)
	@python3 $(CURDIR)/../tools/bin2c.py $< $@ \
	         --source-name $(TARGET)_mb.gba

$(OUTPUT)_mb.gba: $(OUTPUT)_mb.elf
$(OUTPUT)_mb.elf: $(OFILES)

-include $(DEPENDS)

endif
