# LibDragon toolchain image for building Joypad Tester N64 ROMs.
#
# The upstream `ghcr.io/dragonminded/libdragon` image only ships the
# MIPS cross-compiler + binutils + newlib (under /n64_toolchain).
# The LibDragon library itself, the headers, and the `n64.mk` build
# harness all live in the libdragon source repo and need to be
# compiled + installed on top. We do that at image-build time so the
# resulting container is fully self-contained -- `make` inside it just
# works.
#
# The upstream image is amd64-only, so on Apple Silicon / other
# non-x86 hosts the image runs under Docker's amd64 platform
# emulation. `--platform=linux/amd64` is also forwarded by
# build_docker.sh when running the container.
FROM --platform=linux/amd64 ghcr.io/dragonminded/libdragon:latest

ARG LIBDRAGON_REPO=https://github.com/DragonMinded/libdragon.git
ARG LIBDRAGON_REF=trunk

# Need git + the toolchain wrappers to be on PATH already (image
# provides them under /n64_toolchain/bin). Clone libdragon at the
# pinned ref, build the library + install n64.mk into $N64_INST.
RUN apt-get update -qq \
 && apt-get install -y --no-install-recommends git ca-certificates make \
 && rm -rf /var/lib/apt/lists/* \
 && git clone "$LIBDRAGON_REPO" /libdragon \
 && cd /libdragon && git checkout "$LIBDRAGON_REF" \
 && N64_INST=/n64_toolchain ./build.sh \
 && rm -rf /libdragon

# Make the install tree writable by any uid; build_docker.sh runs the
# container as the host user (-u) so output files come back owned by
# them, but the build harness writes a few intermediate files into
# the install tree.
RUN chmod -R a+w /n64_toolchain

WORKDIR /work
