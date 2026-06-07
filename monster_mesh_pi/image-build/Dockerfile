# Monster Nix image-build environment.
#
# This image carries everything build.sh needs on the host side:
#   - qemu-user-static + binfmt   — so we can chroot into an armhf RetroPie image
#                                    from an x86_64 (or aarch64) Docker host.
#   - kpartx + parted             — loop-mount the .img file's boot + root parts.
#   - xz / gzip                   — decompress the base image, recompress output.
#   - wget                        — fetch the base image when --base is a URL.
#
# Cross-compile toolchain (armhf gcc/g++) is included as a fast path so
# customize.sh can optionally skip the in-chroot build; default flow builds
# inside the chroot for self-containedness.
#
# Build:   docker build -t monster-nix-builder .
# Run:     docker run --rm --privileged -v "$PWD/..":/work monster-nix-builder \
#              /work/image-build/build.sh [args]
#
# --privileged is required because we mount loopback devices and bind /dev,
# /proc, /sys into the chroot.  This is standard pi-gen practice.

FROM debian:bookworm

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        # image manipulation
        qemu-user-static \
        binfmt-support \
        kpartx \
        parted \
        dosfstools \
        e2fsprogs \
        util-linux \
        rsync \
        xz-utils \
        gzip \
        # download + checksum
        wget \
        curl \
        ca-certificates \
        coreutils \
        # cross-compile fast-path (optional; customize.sh uses chroot by default)
        gcc-arm-linux-gnueabihf \
        g++-arm-linux-gnueabihf \
        cmake \
        # generic
        sudo \
        bash \
        file \
    && rm -rf /var/lib/apt/lists/*

# binfmt registration is kernel-level (host-side), not container-level.
# Docker Desktop preloads ARM binfmt handlers via its bundled tonistiigi/binfmt
# image, so we don't need to register anything inside this build environment.
# build.sh invokes the chroot ELFs explicitly via `qemu-arm-static /bin/bash …`
# so even when host binfmt is missing, the chroot still works.

WORKDIR /work

# build.sh is the entrypoint when running interactively.  The README documents
# the full command; here we just provide a useful default.
CMD ["/bin/bash"]
