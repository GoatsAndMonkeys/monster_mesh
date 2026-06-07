#!/bin/bash
# build.sh — Bake a Monster Nix flashable SD image.
#
# Runs INSIDE the monster-nix-builder Docker container (see Dockerfile).
# Downloads (or reuses) a RetroPie base .img.gz, expands it, mounts boot+root
# via loop devices, copies our customize.sh + source tree in, chroots through
# qemu-arm-static, runs customize.sh, cleans up, recompresses to .img.xz.
#
# Usage (from host, not inside container):
#   ./build.sh
#       — uses the default RetroPie 4.8 rpi2/rpi3 image as base.
#   ./build.sh --base /path/to/already-downloaded.img.gz
#       — skip the download, use a local file (handy with a GPI-preconfigured
#         community image).
#   ./build.sh --base https://example.com/custom.img.gz
#       — fetch this URL instead.
#   ./build.sh --out /work/dist/monster-nix-v1.img.xz
#       — name the output file.
#
# Output: dist/monster-nix-pi-zero-2w-<git-sha>.img.xz + .sha256 in the work dir.

set -euo pipefail

# ── Defaults ────────────────────────────────────────────────────────────────
# Official RetroPie 4.8 image for Pi 2 / 3 / Zero 2W (Cortex-A53).  RetroPie
# explicitly ships a Zero-2W-tuned variant in this single image — same
# Debian Buster userland, same kernel, optimized boot config for all three.
# Override with --base when a newer RetroPie release lands or when you want
# the community GPI-preconfigured image as your starting point.
DEFAULT_BASE_URL="https://github.com/RetroPie/RetroPie-Setup/releases/download/4.8/retropie-buster-4.8-rpi2_3_zero2w.img.gz"

BASE_INPUT="${DEFAULT_BASE_URL}"
OUT_PATH=""
WORK_ROOT="${WORK_ROOT:-/work/image-build}"
CACHE_DIR="${WORK_ROOT}/.cache"
WORKDIR="${WORK_ROOT}/.build"
DIST_DIR="${WORK_ROOT}/dist"

# ── Arg parsing ─────────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --base) BASE_INPUT="$2"; shift 2 ;;
        --out)  OUT_PATH="$2";   shift 2 ;;
        --help|-h)
            sed -n '2,/^set -euo/p' "$0" | head -n 25
            exit 0
            ;;
        *) echo "Unknown arg: $1" >&2; exit 2 ;;
    esac
done

mkdir -p "$CACHE_DIR" "$WORKDIR" "$DIST_DIR"

# Derive a default OUT_PATH if not provided.
# host-build.sh pre-computes the git sha on the host and passes it via env,
# because the source tarball doesn't include .git.  Fall back to an in-tree
# rev-parse for the legacy bind-mount path, then "dev" as last resort.
if [[ -z "$OUT_PATH" ]]; then
    GIT_SHA="${GIT_SHA_OVERRIDE:-$(git -C /work rev-parse --short HEAD 2>/dev/null || echo dev)}"
    OUT_PATH="${DIST_DIR}/monster-nix-pi-zero-2w-${GIT_SHA}.img.xz"
fi

echo "== Monster Nix image build =="
echo "  Base:   ${BASE_INPUT}"
echo "  Output: ${OUT_PATH}"
echo

# ── Step 1: Obtain base image ───────────────────────────────────────────────
if [[ "$BASE_INPUT" == http*://* ]]; then
    BASE_NAME="$(basename "$BASE_INPUT")"
    BASE_CACHED="${CACHE_DIR}/${BASE_NAME}"
    if [[ ! -f "$BASE_CACHED" ]]; then
        echo "[1/7] Downloading base image…"
        wget -O "$BASE_CACHED.part" "$BASE_INPUT"
        mv "$BASE_CACHED.part" "$BASE_CACHED"
    else
        echo "[1/7] Using cached base image: $BASE_CACHED"
    fi
else
    BASE_CACHED="$BASE_INPUT"
    [[ -f "$BASE_CACHED" ]] || { echo "ERROR: base image $BASE_CACHED not found" >&2; exit 1; }
    echo "[1/7] Using local base image: $BASE_CACHED"
fi

# ── Step 2: Decompress to working .img ──────────────────────────────────────
WORK_IMG="${WORKDIR}/work.img"
echo "[2/7] Expanding base image to ${WORK_IMG}…"
rm -f "$WORK_IMG"
# The base lives on the macOS virtio-fs bind mount (.cache).  Decompressors
# read it with seeks/large buffers that intermittently trip EDEADLK
# ("Resource deadlock avoided") through virtio-fs; a plain sequential `cat`
# into container-local /tmp does not.  Copy first, then decompress locally.
LOCAL_BASE="/tmp/$(basename "$BASE_CACHED")"
echo "  staging base to container-local ${LOCAL_BASE}…"
cat "$BASE_CACHED" > "$LOCAL_BASE"
case "$LOCAL_BASE" in
    *.img.gz)  gunzip -c   "$LOCAL_BASE" > "$WORK_IMG" ;;
    *.img.xz)  xz -dc       "$LOCAL_BASE" > "$WORK_IMG" ;;
    *.img)     cp "$LOCAL_BASE" "$WORK_IMG" ;;
    *) echo "ERROR: unknown image format: $LOCAL_BASE" >&2; rm -f "$LOCAL_BASE"; exit 1 ;;
esac
rm -f "$LOCAL_BASE"

# ── Step 3: Grow image so we have room for our deps + binaries ──────────────
# RetroPie images ship near-full.  We add 600 MiB of headroom for libsdl2,
# the meshtastic Python venv, our binaries, and a couple of placeholder ROMs.
echo "[3/7] Adding 600 MiB headroom to root partition…"
truncate -s +600M "$WORK_IMG"
# Use parted to extend the last (root) partition to fill new space.
ROOT_PART_NUM="$(parted -m "$WORK_IMG" unit s print | tail -n1 | cut -d: -f1)"
parted "$WORK_IMG" --script resizepart "$ROOT_PART_NUM" 100%

# ── Step 4: Mount via kpartx, then resize2fs root ───────────────────────────
echo "[4/7] Loop-mounting partitions…"
# Docker Desktop's LinuxKit VM has only loop0..loop7 by default.  Prior
# failed runs leave device-mapper entries (loopXp1/p2) attached even after
# our cleanup, and as long as those dm entries reference a loop, losetup
# can't detach it.  Tear down both layers — dm entries first, then loops.
echo "  Loops before cleanup:"
losetup -l 2>&1 | sed 's/^/    /' || true
echo "  Releasing leftover device-mapper entries…"
dmsetup remove_all 2>&1 | sed 's/^/    /' || true
echo "  Detaching all loop devices…"
losetup -D 2>&1 | sed 's/^/    /' || true
echo "  Loops after cleanup:"
losetup -l 2>&1 | sed 's/^/    /' || true

# kpartx -av prints "add map loopXp1 …" lines; we parse the loop device.
KPARTX_OUT="$(kpartx -av "$WORK_IMG")"
echo "$KPARTX_OUT"
LOOP_DEV="$(echo "$KPARTX_OUT" | head -n1 | awk '{print $3}' | sed 's/p[0-9]*$//')"
BOOT_MAP="/dev/mapper/${LOOP_DEV}p1"
ROOT_MAP="/dev/mapper/${LOOP_DEV}p2"

# Expand the root fs to fill the newly resized partition.
e2fsck -f -y "$ROOT_MAP" || true
resize2fs "$ROOT_MAP"

MNT="${WORKDIR}/mnt"
mkdir -p "$MNT"
mount "$ROOT_MAP" "$MNT"
mkdir -p "$MNT/boot"
mount "$BOOT_MAP" "$MNT/boot"

# Bind-mount /dev /proc /sys for chroot, plus copy qemu-arm-static into
# /usr/bin so binfmt routes ARM ELFs through it transparently.
mount --bind /dev     "$MNT/dev"
mount --bind /dev/pts "$MNT/dev/pts"
mount --bind /proc    "$MNT/proc"
mount --bind /sys     "$MNT/sys"
cp /usr/bin/qemu-arm-static "$MNT/usr/bin/qemu-arm-static"

# Swap in working DNS for the chroot.  RetroPie ships /etc/resolv.conf as a
# symlink to systemd-resolved's stub (127.0.0.53), which doesn't exist in
# our Docker container, so any apt-get inside the chroot would die with
# "Temporary failure resolving …".  Restore the original on cleanup so the
# final image's runtime resolv.conf isn't ours.
RESOLV="$MNT/etc/resolv.conf"
if [ -e "$RESOLV" ] || [ -L "$RESOLV" ]; then
    mv "$RESOLV" "${RESOLV}.mmbak"
fi
cat > "$RESOLV" <<EOF
# Temporary DNS for in-chroot apt; restored on build cleanup.
nameserver 1.1.1.1
nameserver 8.8.8.8
EOF

# Always clean up mounts on exit, even on failure.
cleanup() {
    echo "== Cleaning up mounts =="
    sync
    # Restore the image's original resolv.conf so it boots with the
    # RetroPie-default DNS resolution, not our build-time override.
    if [ -e "${RESOLV}.mmbak" ] || [ -L "${RESOLV}.mmbak" ]; then
        rm -f "$RESOLV" 2>/dev/null || true
        mv "${RESOLV}.mmbak" "$RESOLV" 2>/dev/null || true
    fi
    umount -lf "$MNT/dev/pts" 2>/dev/null || true
    umount -lf "$MNT/dev"     2>/dev/null || true
    umount -lf "$MNT/proc"    2>/dev/null || true
    umount -lf "$MNT/sys"     2>/dev/null || true
    umount -lf "$MNT/boot"    2>/dev/null || true
    umount -lf "$MNT"         2>/dev/null || true
    rm -f "$MNT/usr/bin/qemu-arm-static" 2>/dev/null || true
    kpartx -dv "$WORK_IMG" 2>/dev/null || true
    # kpartx -d sometimes leaves the dm entries dangling, which then pin the
    # loop device.  Force-remove all dm entries then detach all loops so the
    # next run has a clean LinuxKit VM loop pool to work with.
    dmsetup remove_all 2>/dev/null || true
    losetup -D 2>/dev/null || true
}
trap cleanup EXIT

# ── Step 5: Copy source + customize.sh into chroot ──────────────────────────
echo "[5/7] Staging source tree inside chroot…"
CHROOT_STAGE="$MNT/opt/monstermesh-build"
mkdir -p "$CHROOT_STAGE"
# Allowlist exactly what customize.sh needs.  Avoiding tools/ (host-only
# sprite-bake + Python venv), firmware-builds/ (T-Deck artifacts unrelated
# to the Pi port), and image-build/.cache (the 700 MB base image) keeps us
# off Docker-Desktop virtio-fs's flaky paths.
for sub in src CMakeLists.txt retropie tests; do
    if [ -e "/work/$sub" ]; then
        cp -a "/work/$sub" "$CHROOT_STAGE/"
    fi
done
# image-build/ — copy only the small files customize.sh actually reads
# (config patches + README + helper scripts); skip .cache/.build/dist.
mkdir -p "$CHROOT_STAGE/image-build"
cp -a /work/image-build/config-patches "$CHROOT_STAGE/image-build/" 2>/dev/null || true
cp -a /work/image-build/customize.sh   "$CHROOT_STAGE/image-build/" 2>/dev/null || true
cp -a /work/image-build/build.sh       "$CHROOT_STAGE/image-build/" 2>/dev/null || true
cp -a /work/image-build/Dockerfile     "$CHROOT_STAGE/image-build/" 2>/dev/null || true
cp -a /work/image-build/README.md      "$CHROOT_STAGE/image-build/" 2>/dev/null || true
# Strip any .DS_Store that snuck in.
find "$CHROOT_STAGE" -name .DS_Store -delete 2>/dev/null || true

# customize.sh lives next to this build.sh; the chroot calls it from inside.
cp "${WORK_ROOT}/customize.sh" "$MNT/opt/customize.sh"
chmod +x "$MNT/opt/customize.sh"

# ── Step 6: Run customize.sh inside the chroot ──────────────────────────────
echo "[6/7] Entering chroot to customize…"
chroot "$MNT" /usr/bin/qemu-arm-static /bin/bash /opt/customize.sh

# Drop the source tree from the final image — binaries + configs are all the
# user needs at runtime, and the source bloats the .img by ~50MB.
rm -rf "$CHROOT_STAGE"

# ── Step 7: Unmount, compress, checksum ─────────────────────────────────────
echo "[7/7] Finalizing image…"
cleanup
trap - EXIT

# Compress with xz -T0 (parallel) at default level 6 — ~2-3x faster than
# level 9 with a few % size hit.  Image goes ~3GB → ~700MB.
echo "Compressing to $OUT_PATH (this takes a few minutes)…"
xz -T0 -v -c "$WORK_IMG" > "$OUT_PATH"

# sha256 alongside, so users can verify downloads.
( cd "$(dirname "$OUT_PATH")" && sha256sum "$(basename "$OUT_PATH")" > "$(basename "$OUT_PATH").sha256" )

echo
echo "== DONE =="
ls -lh "$OUT_PATH" "$OUT_PATH.sha256"
echo
echo "Flash with:"
echo "  xz -d $OUT_PATH"
echo "  # then use Balena Etcher to write the resulting .img to your SD card"
