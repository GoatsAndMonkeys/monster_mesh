# Monster Nix — Pi image build

Bakes a flashable `.img.xz` containing **RetroPie + Monster Nix** preconfigured
for the **RetroFlag GPI Case 2W** (Raspberry Pi Zero 2W).  Flash, boot, play.

## What's in the image

- RetroPie 4.8 (Buster) — the official `rpi2_3_zero2w` image, tuned for Pi Zero 2W
- Monster Nix daemon (`mmd`) + terminal (`mmterm`) installed to `/opt/monstermesh/bin/`
- `monstermesh.service` systemd unit (starts daemon on boot)
- EmulationStation entry — MonsterMesh appears as its own "console" in the carousel
- Python venv at `/opt/monstermesh/.venv` with the `meshtastic` library
- GPI Case 2W boot overlays (LCD, audio, power switch) in `/boot/config.txt`
- `pi` user added to `dialout` for serial-port access

## Prerequisites

- **Docker Desktop** (Mac/Windows/Linux) — needed because chroot + binfmt
  require Linux.  Install from [docker.com](https://www.docker.com/products/docker-desktop/).
- ~5 GB free disk (base image + working copy + output)
- ~30–60 minutes of build time (most of it is in-chroot compile via qemu)

## Build

From the `monster_mesh_pi/` root:

```sh
# One-time: build the builder image
docker build -t monster-nix-builder image-build/

# Bake the image
./image-build/host-build.sh
```

Output lands at `image-build/dist/monster-nix-pi-zero-2w-<git-sha>.img.xz`
alongside a `.sha256` for verification.

> **Why a wrapper script instead of `docker run` directly?** Docker Desktop
> on macOS uses virtio-fs for bind mounts, and reading many small files
> through it intermittently fails with `EDEADLK` ("Resource deadlock
> avoided").  `host-build.sh` tars the source tree on the host and bind-
> mounts only the resulting single file, sidestepping the issue.  Linux
> Docker hosts don't have this bug, but the wrapper still works there.

### Build options

| Flag | Default | What it does |
|---|---|---|
| `--base <url-or-path>` | RetroPie 4.8 rpi2_3_zero2w image | Use a different base.  Pass a path to skip the download or a URL to fetch fresh. |
| `--out <path>` | `dist/monster-nix-pi-zero-2w-<sha>.img.xz` | Override the output filename. |

Environment knobs (set with `-e` on `docker run`):

| Var | Default | What it does |
|---|---|---|
| `MONSTERMESH_AUTOLAUNCH` | `0` | When `1`, boot straight into MonsterMesh instead of EmulationStation menu. |

### Using the GPI-preconfigured community image as a base

The [community "GPi Zero 2 v1.52" image](https://retropie.org.uk/forum/topic/31708/gpi-zero-2-v1-52-gpi-zero-v1-15-retropie-images-for-pi-zero-zero2-gpi-case-1-gpi-case-2w)
already has the GPI Case 2W overlays configured.  If you start there, our
`/boot/config.txt` patches are idempotent — they detect the marker comment
and skip if already present.

```sh
./image-build/host-build.sh --base /work/image-build/.cache/gpi-zero-2-v1.52.img
```

## Flash

```sh
# Decompress
xz -d image-build/dist/monster-nix-pi-zero-2w-<sha>.img.xz

# Then use Balena Etcher (https://etcher.balena.io/) to write
# the resulting .img to your microSD card.  Etcher works on Mac, Windows,
# and Linux and handles the GPI's small SD partition layout cleanly.
```

CLI alternative (Mac):

```sh
diskutil list                                # find the SD card's disk number
diskutil unmountDisk /dev/diskN              # unmount but don't eject
sudo dd if=monster-nix-pi-zero-2w-<sha>.img \
        of=/dev/rdiskN bs=4m status=progress # rdiskN (raw) is ~10× faster
sync
diskutil eject /dev/diskN
```

## First boot

1. Insert SD into the GPI Case 2W
2. Slide the power switch on
3. Wait ~60s for first-boot expansion (one-time only)
4. EmulationStation appears — scroll the carousel, find **MonsterMesh**
5. Press the action button; the daemon is already running, `mmterm` launches
6. Plug in your nRF52 mesh node via USB-C — daemon auto-detects

If you set `MONSTERMESH_AUTOLAUNCH=1` at build time, step 4 happens automatically.

## What if it doesn't boot

- **Black screen** — usually GPI Case overlay mismatch.  SSH in over USB-ethernet
  (gadget mode is enabled by default in RetroPie) and `tail /var/log/syslog`.
- **HDMI output instead of GPI LCD** — confirm `dtoverlay=dpi24` is in
  `/boot/config.txt`.  Our patches set this; some base images strip it.
- **No serial node detected** — `groups pi` should list `dialout`; if not,
  `sudo usermod -a -G dialout pi && sudo reboot`.

## Build internals

- `Dockerfile` — Debian Bookworm with qemu-user-static + kpartx + cross-toolchain
- `build.sh` — host orchestration: download → loop-mount → chroot → repack
- `customize.sh` — runs inside the chroot: apt + build + install + configure
- `config-patches/config.txt.append` — GPI Case 2W boot overlays

The build runs `customize.sh` under qemu-arm-static so all package installs
and the `cmake + make` step produce native armhf binaries.  This is slower
than cross-compiling (10–20 minutes) but avoids cross-sysroot dependency
hell.  If you want the cross-compile fast path later, the toolchain is
preinstalled in the Dockerfile — just set `CMAKE_TOOLCHAIN_FILE` and skip
the chroot build step.

## Distribution

CI builds the same image on every tag and publishes to GitHub Releases —
see `.github/workflows/build-image.yml`.  End users just download the
released `.img.xz` and flash; they never need Docker.
