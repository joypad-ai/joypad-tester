# Joypad Tester - NUON
#
# Builds a signed nuon.run + bootable UDF DVD ISO from src/main.c using
# the cubanismo/nuon-sdk toolchain. Assumes env.sh from that repo has
# been sourced (sets $VMLABS, $VMBLESSDIR, $VMHOSTARCH, $PATH); the
# build_docker.sh wrapper handles that automatically inside its
# container. Run `make` directly if you've sourced env.sh yourself.

TARGET   := joypad-tester
SRCDIR   := src
BUILDDIR := build
ISODIR   := $(BUILDDIR)/iso

CFILES   := main.c
OBJS     := $(CFILES:%.c=$(BUILDDIR)/%.o)

# -mrom: target the on-cartridge ROM section. -mpe3: schedule for
# Processing Element 3 (the one running C code). -mreopt: turn on the
# NUON-specific reoptimiser pass. -O3 -Wall match the SDK samples.
CFLAGS   := -O3 -g -mreopt -Wall -mrom -mpe3
LDFLAGS  := -mrom -mpe3
LIBS     := -lmml2d -lmltxt -lmutil

COF       := $(BUILDDIR)/$(TARGET).cof
STRIPPED  := $(BUILDDIR)/$(TARGET).stripped.cof
PACKED    := $(BUILDDIR)/$(TARGET).packed.cof
SIGNED    := $(BUILDDIR)/nuon.run
ISO       := $(BUILDDIR)/$(TARGET).iso

.PHONY: all clean iso

all: $(ISO)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(BUILDDIR)/%.o: $(SRCDIR)/%.c | $(BUILDDIR)
	mgcc $(CFLAGS) -c $< -o $@

$(COF): $(OBJS)
	mgcc $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

# Strip debug info / symbols (smaller .cof) -> COFF-pack
# (LZSS-compress, required to fit in NUON's 4.5MB CD limit and to keep
# DVD load times reasonable) -> vmmakeapp (signs with the 2022-
# recovered Application key) -> mv to nuon.run (the filename retail
# players look for in /NUON/).
$(STRIPPED): $(COF)
	vmstrip -F -o $@ $<

$(PACKED): $(STRIPPED)
	coffpack -o $@ $<

$(SIGNED): $(PACKED)
	cd $(BUILDDIR) && vmmakeapp $(TARGET).packed.cof 0
	mv $(BUILDDIR)/$(TARGET).packed.cof.app $@

# UDF ISO with /NUON/nuon.run at the root. Boots on retail NUON-
# enhanced DVD players (Samsung N501/N504/N505, RCA DRC300N/DRC480N).
# Toshiba SD2300 owners may need the macOS hdiutil "Universal" ISO
# variant (built by the release workflow alongside this one).
$(ISO): $(SIGNED)
	rm -rf $(ISODIR)
	mkdir -p $(ISODIR)/NUON
	cp $(SIGNED) $(ISODIR)/NUON/nuon.run
	genisoimage -udf -iso-level 4 -V "JOYPAD_TESTER" \
	    -o $@ $(ISODIR)

iso: $(ISO)

clean:
	rm -rf $(BUILDDIR)
