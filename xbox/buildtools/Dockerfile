# nxdk toolchain image extended with USB Mass Storage + Audio class
# drivers baked in.
#
# The upstream ghcr.io/xboxdev/nxdk:latest only pre-builds the XID
# game-controller class driver; the MSC and UAC class drivers exist
# in the libusbohci source tree but aren't compiled unless the build
# is run with NXDK_USB_ENABLE_MSC=y / NXDK_USB_ENABLE_UAC=y.
#
# Memory Units are USB MSC (xboxdevwiki.net/Xbox_Memory_Unit) and the
# Communicator / Headset are USB UAC, so we need both. This Dockerfile
# re-runs the nxdk-internal build with those feature flags, then bakes
# the resulting .obj/.lib back into /usr/src/nxdk so the app-level
# build can link against them.
FROM --platform=linux/amd64 ghcr.io/xboxdev/nxdk:latest

USER root
# nxdk's runbase image is alpine -- no bash, so we invoke its
# `bin/activate -s` sh-style and feed it back into eval. Building the
# additional class drivers (~5 min on Apple Silicon via Rosetta).
RUN cd /usr/src/nxdk && \
    sh -c 'eval $(./bin/activate -s); \
           make NXDK_ONLY=y \
                NXDK_USB_ENABLE_MSC=y \
                NXDK_USB_ENABLE_UAC=y \
                NXDK_USB_ENABLE_HID=y \
                -j$(nproc)' && \
    chmod -R a+rwX /usr/src/nxdk
