# cubanismo/nuon-sdk toolchain image for building Joypad Tester NUON
# ROMs and signed DVD ISOs.
#
# The VM Labs NUON SDK ships as 32-bit x86 Linux ELF binaries (mgcc,
# vmld, llama, vmstrip, coffpack) and a small set of 32-bit x86
# binaries from the "blessing" framework (blessapp, padapp, gpg) that
# sign nuon.run payloads for retail-DVD boot. Everything just runs
# natively in an i386 base image -- no libc6-i386 multiarch dance, no
# wine for the DVD path.
#
# On Apple Silicon / non-x86 hosts the i386 binaries run via Docker's
# linux/386 platform emulation. CreateNuonCD (the CD-R/Samsung path)
# is Windows-only and would need wine; we skip it because v0.1.0
# ships a signed DVD-R ISO via vmmakeapp, which is Linux-native.
FROM --platform=linux/386 i386/debian:bullseye-slim

RUN apt-get update -qq \
 && apt-get install -y --no-install-recommends \
        ca-certificates \
        make \
        genisoimage \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /work
