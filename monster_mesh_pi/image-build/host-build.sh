#!/bin/bash
# host-build.sh — Host wrapper for the Monster Nix image build.
#
# Why this exists: Docker Desktop on macOS uses virtio-fs for bind mounts,
# and reading many small files through that mount intermittently fails with
# errno 35 (EDEADLK, "Resource deadlock avoided").  Tools that walk a tree
# (rsync, cp -a, even tar) hit it.  Single-large-file reads (the 700 MB
# base .img.gz, the working .img) are fine.
#
# Sidestep: tar the source tree on the host (fast, native APFS), bind-mount
# only single big files into the container — the source tarball, the cache
# dir (which only holds the base .img.gz), the dist dir (output .img.xz).
# Inside the container, extract the tar onto overlayfs.  Overlayfs has no
# virtio-fs in the path, so cp/rsync of small files works normally.
#
# Usage (from monster_mesh_pi/):
#   ./image-build/host-build.sh
#   ./image-build/host-build.sh --base /work/image-build/.cache/something.img.gz
#   MONSTERMESH_AUTOLAUNCH=1 ./image-build/host-build.sh

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IB="$REPO_ROOT/image-build"
mkdir -p "$IB/.cache" "$IB/dist"

# Tarball lives on the host's /tmp (APFS, no Docker FS involved).  Single
# file → single bind mount → virtio-fs handles it without issue.
SRCTAR="$(mktemp -t mm-srctree.XXXXXXXX.tar)"
trap 'rm -f "$SRCTAR"' EXIT

# Pre-compute git sha here; the container won't have .git in the tarball,
# so build.sh's `git rev-parse` would fall back to "dev" otherwise.
GIT_SHA="$(git -C "$REPO_ROOT" rev-parse --short HEAD 2>/dev/null || echo dev)"

echo "== [host] Tarring source tree =="
# Allowlist what customize.sh / build.sh actually need.  Skipping tools/
# (host-only sprite-bake + 302-image .cache), firmware-builds/ (T-Deck
# artifacts), and image-build/.cache / .build / dist (regenerable).
( cd "$REPO_ROOT" && tar \
    --exclude='.DS_Store' \
    --exclude='__pycache__' \
    -cf "$SRCTAR" \
    src CMakeLists.txt retropie tests \
    image-build/build.sh \
    image-build/customize.sh \
    image-build/config-patches \
    image-build/README.md \
)
echo "Source tarball:"; ls -lh "$SRCTAR"

echo "== [host] Launching builder container =="
# Only single-file or single-large-file bind mounts:
#   /srctree.tar               — our tarball (one big file)
#   /work/image-build/.cache   — base RetroPie .img.gz (one big file)
#   /work/image-build/dist     — output .img.xz (one big file)
# WORK_ROOT is unset so build.sh defaults to /work/image-build, which is
# fine: .cache and dist are bind-mounted there, .build is overlayfs.
docker run --rm --privileged \
    --dns=1.1.1.1 --dns=8.8.8.8 \
    -v "$IB/.cache:/work/image-build/.cache" \
    -v "$IB/dist:/work/image-build/dist" \
    -v "$SRCTAR:/srctree.tar:ro" \
    -e GIT_SHA_OVERRIDE="$GIT_SHA" \
    ${MONSTERMESH_AUTOLAUNCH:+-e MONSTERMESH_AUTOLAUNCH="$MONSTERMESH_AUTOLAUNCH"} \
    monster-nix-builder \
    /bin/bash -c '
        set -euo pipefail
        mkdir -p /work
        echo "[container] Extracting source tarball into /work…"
        tar -xf /srctree.tar -C /work
        echo "[container] /work layout:"
        ls -la /work
        echo "[container] Handing off to build.sh"
        exec /work/image-build/build.sh "$@"
    ' -- "$@"
