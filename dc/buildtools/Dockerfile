# KallistiOS toolchain image for Joypad Tester Dreamcast.
#
# Mirrors the upstream KOS Dockerfile pattern (Alpine + kos-chain) and
# adds two layers on top:
#   1. Builds KallistiOS itself (the upstream image only builds the
#      sh-elf + arm-eabi cross compilers, not KOS — see
#      utils/kos-chain/docker/README.md).
#   2. Installs mkdcdisc (gitlab.com/synthicyde/mkdcdisc), the modern
#      one-shot `.elf -> selfboot .cdi` packager.
#
# First build is slow (~15-25 min: cross compilers from source, then KOS).
# Layer cache covers subsequent rebuilds. Tunable via build args:
#
#   docker build --build-arg KOS_REF=<sha> \
#                --build-arg KOS_PORTS_REF=<sha> \
#                --build-arg MKDCDISC_REF=<sha> \
#                -t joypad-tester-kos:latest dc/buildtools/
#
FROM alpine:3.20

# kos-chain prerequisites, copied from the upstream KOS Dockerfile at
# utils/kos-chain/docker/Dockerfile. The extras (meson, ninja, make,
# sed, gawk) are for mkdcdisc + our Makefile glue.
RUN apk --update add --no-cache \
        bison \
        build-base \
        coreutils \
        gmp-dev \
        mpfr-dev \
        mpc1-dev \
        patch \
        bash \
        texinfo \
        flex \
        curl \
        wget \
        git \
        subversion \
        elfutils-dev \
        libjpeg-turbo-dev \
        libpng-dev \
        python3 \
        meson \
        ninja \
        make \
        sed \
        gawk \
    && rm -rf /var/cache/apk/*

ARG KOS_REPO=https://github.com/KallistiOS/KallistiOS.git
ARG KOS_REF=master
ARG KOS_PORTS_REPO=https://github.com/KallistiOS/kos-ports.git
ARG KOS_PORTS_REF=master

# Clone KOS + kos-ports into the canonical /opt/toolchains/dc/ layout.
# Not depth=1 because the dc-chain build sometimes patches against
# tags / refs that aren't in a shallow clone.
RUN mkdir -p /opt/toolchains/dc \
 && git clone "$KOS_REPO" /opt/toolchains/dc/kos \
 && cd /opt/toolchains/dc/kos \
 && git checkout "$KOS_REF" \
 && git clone "$KOS_PORTS_REPO" /opt/toolchains/dc/kos-ports \
 && cd /opt/toolchains/dc/kos-ports \
 && git checkout "$KOS_PORTS_REF"

# Build the sh-elf (SH4 main CPU) + arm-eabi (AICA sound CPU) cross
# compilers via kos-chain. The 'stable' profile is GCC 15.2.0 as of
# 2025-08-08 per utils/kos-chain/Makefile.dreamcast.cfg. Outputs land
# at /opt/toolchains/dc/sh-elf and /opt/toolchains/dc/arm-eabi.
RUN cd /opt/toolchains/dc/kos/utils/kos-chain \
 && make build platform=dreamcast toolchain_profile=stable

# Set up environ.sh from the upstream sample. Its default paths
# already match /opt/toolchains/dc/, so no edits are needed for this
# layout. environ.sh sources environ_base.sh which sources
# environ_dreamcast.sh — that's the full KOS env chain.
RUN cp /opt/toolchains/dc/kos/doc/environ.sh.sample \
       /opt/toolchains/dc/kos/environ.sh

ENV KOS_BASE=/opt/toolchains/dc/kos
ENV KOS_PORTS=/opt/toolchains/dc/kos-ports
ENV PATH=/opt/toolchains/dc/sh-elf/bin:/opt/toolchains/dc/arm-eabi/bin:${PATH}

# Build KOS itself. Sourcing environ.sh sets up KOS_CC / KOS_CFLAGS
# etc. so that the top-level Makefile finds the cross compilers.
RUN bash -c "source $KOS_BASE/environ.sh \
        && cd $KOS_BASE \
        && make"

# mkdcdisc — gitlab.com/synthicyde/mkdcdisc, the canonical maintained
# fork. Subsumes scramble + makeip + objcopy + cdi4dc into one tool.
# Default IP.BIN ships with VGA-compatibility bits set, so no
# extra flags are needed for our use case.
#
# Kept in its own layer so the prereq apk install doesn't invalidate
# the ~25-minute toolchain layer above.
RUN apk --update add --no-cache \
        libisofs-dev \
        pkgconfig \
    && rm -rf /var/cache/apk/*

ARG MKDCDISC_REPO=https://gitlab.com/synthicyde/mkdcdisc.git
ARG MKDCDISC_REF=main
RUN git clone "$MKDCDISC_REPO" /opt/mkdcdisc \
 && cd /opt/mkdcdisc \
 && git checkout "$MKDCDISC_REF" \
 && meson setup build \
 && ninja -C build \
 && cp build/mkdcdisc /usr/local/bin/ \
 && rm -rf /opt/mkdcdisc

# Wrapper so plain `make` from /workspace/dc picks up the KOS env
# without each invocation having to source environ.sh manually.
RUN printf '%s\n' \
    '#!/usr/bin/env bash' \
    'set -e' \
    'source /opt/toolchains/dc/kos/environ.sh' \
    'exec "$@"' \
    > /usr/local/bin/kos-env \
 && chmod +x /usr/local/bin/kos-env

WORKDIR /workspace
ENTRYPOINT ["/usr/local/bin/kos-env"]
CMD ["bash"]
