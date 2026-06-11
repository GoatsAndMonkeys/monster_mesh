#!/bin/bash
# customize.sh — Runs inside the chroot of the RetroPie base image.
#
# Invoked by build.sh from the host side; by the time this script runs:
#   - / is the mounted root partition of the .img
#   - /boot is the FAT boot partition
#   - /dev, /proc, /sys are bind-mounted
#   - qemu-arm-static is at /usr/bin/qemu-arm-static so ARM binaries Just Work
#   - the full monster_mesh_pi source tree is at /opt/monstermesh-build
#
# What we do in here:
#   1. apt-install runtime + build deps
#   2. compile mmd + mmterm from source (slow in qemu — that's expected)
#   3. install binaries to /opt/monstermesh/bin, launcher, systemd unit
#   4. register the MonsterMesh "console" in EmulationStation
#   5. set up the Python meshtastic relay in a venv
#   6. configure GPI Case 2W boot overlays
#   7. add pi user to dialout (serial port access)
#   8. (optional) auto-launch MonsterMesh on boot

set -euo pipefail

echo "== customize.sh: starting inside chroot =="
echo "Architecture: $(uname -m)  Kernel: $(uname -r)"

# ── 0. Force working DNS ────────────────────────────────────────────────────
# Belt-and-suspenders DNS: even if build.sh swapped resolv.conf, RetroPie's
# /etc/resolv.conf is sometimes a stale symlink that survives `mv` weirdly,
# and qemu-arm-static occasionally caches stale name resolution.  Rewriting
# here, AFTER chroot entry, guarantees apt sees public DNS regardless.
echo "[0/8] Forcing /etc/resolv.conf to public DNS…"
ls -la /etc/resolv.conf || true
rm -f /etc/resolv.conf
cat > /etc/resolv.conf <<EOF
# Temporary DNS for in-chroot apt; build.sh restores the original at cleanup.
nameserver 1.1.1.1
nameserver 8.8.8.8
EOF
echo "resolv.conf is now:"; cat /etc/resolv.conf
echo "DNS sanity check (getent hosts raspbian.raspberrypi.org):"
getent hosts raspbian.raspberrypi.org || echo "WARN: getent failed — apt will likely fail too"

# ── 0b. Patch apt sources for archived Buster repos ─────────────────────────
# RetroPie 4.8 is Raspbian Buster (Debian 10), which went archive-only in
# 2024.  raspbian.raspberrypi.org (the Pi Foundation's CDN mirror) dropped
# Buster, and so did archive.raspbian.org (404 on dists/buster/Release).  The
# host that STILL serves Buster armhf is legacy.raspbian.org — point apt there.
#
# CRITICAL — DO NOT add archive.debian.org/debian buster as a fallback here.
# Debian armhf packages are built for ARMv7 (Cortex-A7 baseline); they run on a
# Pi Zero 2 W (ARMv7) but SIGILL / segfault on a genuine Pi Zero W (ARMv6).  A
# previous build pointed at the 404'ing archive.raspbian.org, the Raspbian
# `apt-get update` silently failed, and apt pulled kbd (setfont), the entire
# ncurses family (libncursesw.so.6!), libSDL2_image, libcrypto, etc. from the
# Debian fallback → ~160 ARMv7 files → the Zero W image crashed on boot
# (`setfont` segfault, `mmterm` Illegal instruction).  legacy.raspbian.org has
# every package we need as proper ARMv6, so no Debian source is required.  See
# memory/armv6-image-contamination.md.
echo "[0b/8] Patching apt sources for archived Buster repos (legacy.raspbian.org)…"

# Buster signatures have expired; don't refuse updates over it.
cat > /etc/apt/apt.conf.d/99-monsternix-archive <<APT_EOF
Acquire::Check-Valid-Until "false";
APT_EOF

# Point every Raspbian source at the one host that still serves Buster armhf.
# Cover all the historical CDN/mirror hostnames so the rewrite is robust no
# matter which one the base image shipped with.
for f in /etc/apt/sources.list /etc/apt/sources.list.d/*.list; do
    [ -f "$f" ] || continue
    sed -i -E 's|https?://(raspbian\.raspberrypi\.org\|mirrordirector\.raspbian\.org\|archive\.raspbian\.org)/raspbian|http://legacy.raspbian.org/raspbian|g' "$f"
done

# Belt-and-suspenders: nuke any Debian-archive fallback a prior build may have
# left in the base image — it is the ARMv7 contamination source.
rm -f /etc/apt/sources.list.d/debian-buster-archive.list

echo "Current apt sources:"
grep -rh '^deb ' /etc/apt/sources.list /etc/apt/sources.list.d/ 2>/dev/null || true

# ── 1. apt-install deps ─────────────────────────────────────────────────────
echo "[1/8] Installing runtime + build deps…"
# DEBIAN_FRONTEND keeps debconf quiet during qemu-emulated installs.
export DEBIAN_FRONTEND=noninteractive

# Now that sources point at legacy.raspbian.org, apt-get update MUST succeed.
# A previous build treated this as non-fatal and silently fell back to the
# Debian (ARMv7) archive, producing an image that crashed on real ARMv6
# hardware.  Fail loudly instead of shipping a contaminated image.
if ! apt-get update; then
    echo "FATAL: apt-get update failed — Raspbian Buster archive unreachable." >&2
    echo "Refusing to build: apt would fall back to non-Raspbian (ARMv7) packages." >&2
    exit 1
fi

apt-get install -y --no-install-recommends \
    libsdl2-2.0-0 \
    libsdl2-image-2.0-0 \
    libncurses6 \
    libncursesw6 \
    python3 \
    python3-venv \
    python3-pip \
    git \
    cmake \
    build-essential \
    libsdl2-dev \
    libsdl2-image-dev \
    libncurses-dev \
    pkg-config \
    kbd \
    console-setup

# ── 1b. ARMv6 contamination guard ───────────────────────────────────────────
# Detect the base image's CPU baseline from a known base binary, then assert
# that nothing apt just installed is built for a HIGHER arch.  This is the
# canary that would have caught the archive.raspbian.org → Debian/ARMv7 fiasco
# before it ever reached hardware: a Zero W base is ARMv6, so an ARMv7 (v7)
# library/binary = a package that came from the wrong (Debian) archive and
# WILL SIGILL/segfault on real ARMv6 silicon.
echo "[1b/8] Verifying installed packages match the base CPU arch…"
cpu_arch() { readelf -A "$1" 2>/dev/null | sed -n 's/.*Tag_CPU_arch: *//p' | head -1; }
BASE_ARCH="$(cpu_arch /bin/bash)"
echo "  base userland (/bin/bash) is: ${BASE_ARCH:-unknown}"
# NOTE: libcrypto.so.1.1 (OpenSSL) is deliberately NOT checked.  It is tagged
# Tag_CPU_arch v7 because it ships NEON/ARMv7 assembly, but OpenSSL selects
# those paths at RUNTIME via HWCAP/CPU detection and falls back to ARMv6 — the
# official RetroPie rpi1_zero (Zero W, ARMv6) base image itself ships libcrypto
# as v7 and boots fine.  mmterm doesn't link it anyway.  Flagging it is a false
# positive.  We check only the libs that genuinely SIGILL on ARMv6 (no runtime
# dispatch): the ncurses family, SDL, and the kbd console tools.
if [ "$BASE_ARCH" = "v6" ] || [ "$BASE_ARCH" = "v6KZ" ]; then
    BAD=""
    for f in \
        "$(command -v setfont || echo /usr/bin/setfont)" \
        "$(command -v kbd_mode || echo /usr/bin/kbd_mode)" \
        /usr/lib/arm-linux-gnueabihf/libncursesw.so.6 \
        /usr/lib/arm-linux-gnueabihf/libtinfo.so.6 \
        /usr/lib/arm-linux-gnueabihf/libpanelw.so.6 \
        /usr/lib/arm-linux-gnueabihf/libSDL2-2.0.so.0 \
        /lib/arm-linux-gnueabihf/libSDL2-2.0.so.0 \
        /usr/lib/arm-linux-gnueabihf/libSDL2_image-2.0.so.0 \
        /lib/arm-linux-gnueabihf/libSDL2_image-2.0.so.0; do
        [ -e "$f" ] || continue
        A="$(cpu_arch "$f")"
        case "$A" in
            v7|v7e|v8|v8-a) echo "  CONTAMINATED ($A): $f"; BAD="yes" ;;
        esac
    done
    if [ -n "$BAD" ]; then
        echo "FATAL: ARMv7 packages found on an ARMv6 base — apt used the wrong archive." >&2
        echo "These binaries SIGILL on a real Pi Zero W.  See memory/armv6-image-contamination.md." >&2
        exit 1
    fi
    echo "  OK — all checked runtime files are ARMv6."
else
    echo "  base is ${BASE_ARCH:-non-v6}; skipping ARMv6 contamination guard."
fi

# ── 2. Compile mmd + mmterm from source ─────────────────────────────────────
echo "[2/8] Building Monster Nix from source (slow under qemu; expect 10-20 min)…"

# Buster ships cmake 3.16.3 which has a known bug under qemu-arm-static:
# CMakeCompilerIdDetection.cmake line 26 does
#     list(REMOVE_ITEM lang_files ${nonlang_files})
# unconditionally.  Under qemu, the file(GLOB) for `nonlang_files` two lines
# up matches zero files (Buster's cmake module dir lacks the cross-language
# Compiler/*-DetermineCompiler.cmake files for whatever language CMake is
# probing), so `${nonlang_files}` is empty and `list(REMOVE_ITEM x)` with no
# items errors with "requires two or more arguments".  Those errors cascade
# into CMAKE_C_COMPILER_ID_CONTENT being empty → generated CMakeCCompilerId.c
# references undefined macro COMPILER_ID → won't compile → final fatal.
# Newer cmake (3.20+) guards the REMOVE_ITEM; we patch the one bad line.
echo "[2a/8] Patching system cmake 3.16 compiler-ID bug…"
python3 - <<'PY'
import pathlib, re
p = pathlib.Path("/usr/share/cmake-3.16/Modules/CMakeCompilerIdDetection.cmake")
text = p.read_text()

if "if(nonlang_files)" in text:
    print("Already patched.")
else:
    # Match `list(REMOVE_ITEM lang_files ${nonlang_files})` with any leading
    # whitespace and tolerant quoting.
    pattern = re.compile(
        r'^(?P<indent>[\t ]*)list\(\s*REMOVE_ITEM\s+lang_files\s+"?\$\{nonlang_files\}"?\s*\)\s*$',
        re.MULTILINE)
    m = pattern.search(text)
    if not m:
        # Print actual context so we can see what differs.
        lines = text.split("\n")
        print("--- Lines 18-32 ---")
        for i, line in enumerate(lines[17:32], start=18):
            print(f"{i:3}: {line!r}")
        print("--- end ---")
        raise SystemExit("WARN: REMOVE_ITEM lang_files pattern not found; not patching.")
    indent = m.group("indent")
    replacement = (
        f"{indent}if(nonlang_files)\n"
        f"{indent}  list(REMOVE_ITEM lang_files ${{nonlang_files}})\n"
        f"{indent}endif()"
    )
    new_text = text[:m.start()] + replacement + text[m.end():]
    p.write_text(new_text)
    print(f"Patched at offset {m.start()}.")
PY

SRC=/opt/monstermesh-build
BUILD=${SRC}/build
# Wipe any stale CMakeCache from a previous failed configure — otherwise new
# -D flags get ignored and we'd debug the wrong thing.
rm -rf "$BUILD"
mkdir -p "$BUILD"
cd "$BUILD"

# Diagnostics: confirm the ncurses dev .so + headers actually landed somewhere
# CMake's FindCurses module will search.  On a fresh Buster armhf chroot
# you expect /usr/lib/arm-linux-gnueabihf/libncursesw.so and ncurses.h in
# /usr/include/.
echo "=== Curses inventory ==="
ls -la /usr/lib/arm-linux-gnueabihf/libncurses* 2>/dev/null || echo "no libncurses* in arm-linux-gnueabihf/"
ls -la /usr/lib/libncurses* 2>/dev/null || true
ls -la /usr/include/ncurses.h /usr/include/curses.h 2>/dev/null || true

# CRITICAL: link the *wide* ncurses (libncursesw), not the narrow libncurses.
# mmterm draws its frames with box()/ACS_* and emits UTF-8 sprite glyphs.  On a
# UTF-8 console the NARROW library outputs the VT100 alt-charset letters as
# literal text ("qqqxxxlkmj") instead of ┌─┐│└┘; only the WIDE library encodes
# the real UTF-8 box-drawing codepoints.  CURSES_NEED_WIDE asks FindCurses for
# ncursesw.
#
# Under qemu-emulated chroot, CMake's automatic multi-arch path detection
# (which shells out to dpkg-architecture) often returns empty, so its
# find_library() can't see /usr/lib/arm-linux-gnueabihf/.  Force the arch
# explicitly and also hand-feed the WIDE curses paths so we don't depend on
# find_library finding them.
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_LIBRARY_ARCHITECTURE=arm-linux-gnueabihf \
      -DCURSES_NEED_WIDE=TRUE \
      -DCURSES_INCLUDE_PATH=/usr/include \
      -DCURSES_NCURSES_LIBRARY=/usr/lib/arm-linux-gnueabihf/libncursesw.so \
      -DCURSES_LIBRARY=/usr/lib/arm-linux-gnueabihf/libncursesw.so \
      -DCURSES_LIBRARIES=/usr/lib/arm-linux-gnueabihf/libncursesw.so \
      .. || {
    # CMakeError.log dies with the chroot on cleanup; surface it now so we
    # can see *why* configure failed instead of just "Configuring incomplete".
    echo
    echo "===================== CMake configure FAILED ====================="
    echo "=== CMakeError.log ==="
    cat CMakeFiles/CMakeError.log 2>/dev/null || echo "(no CMakeError.log)"
    echo "=== CMakeOutput.log (tail 200) ==="
    tail -200 CMakeFiles/CMakeOutput.log 2>/dev/null || echo "(no CMakeOutput.log)"
    echo "=================================================================="
    exit 1
}
# -j1 because qemu-emulated multi-thread builds tend to deadlock on weak boxes;
# the few-minute parallelism win isn't worth the flakiness during image-bake.
make -j1 mmd mmterm
file mmd mmterm

# Our own binaries must match the base CPU arch too.  gcc on Buster defaults to
# the Raspbian ARMv6 baseline, but assert it so a toolchain/flag regression
# can't silently produce ARMv7 code that crashes the Zero W.
if [ "$BASE_ARCH" = "v6" ] || [ "$BASE_ARCH" = "v6KZ" ]; then
    for b in mmd mmterm; do
        A="$(cpu_arch "$b")"
        case "$A" in
            v7|v7e|v8|v8-a)
                echo "FATAL: built $b is $A but base is ARMv6 — would SIGILL on a Pi Zero W." >&2
                exit 1 ;;
        esac
        echo "  $b CPU arch: ${A:-unknown} (OK for ARMv6 base)"
    done
fi

# ── 3. Install binaries + launcher + systemd unit ──────────────────────────
echo "[3/8] Installing binaries to /opt/monstermesh…"
install -d /opt/monstermesh/bin
install -m 0755 mmd      /opt/monstermesh/bin/mmd
install -m 0755 mmterm   /opt/monstermesh/bin/mmterm
install -m 0755 "${SRC}/retropie/launch.sh"          /opt/monstermesh/bin/launch.sh
install -m 0644 "${SRC}/retropie/monstermesh.service" /etc/systemd/system/monstermesh.service
# Admin command: reset Legend of Charizard gym/league progress (callable as
# `mm-reset-gyms` from any shell).
if [ -f "${SRC}/retropie/mm-reset-gyms" ]; then
    install -m 0755 "${SRC}/retropie/mm-reset-gyms" /opt/monstermesh/bin/mm-reset-gyms
    ln -sf /opt/monstermesh/bin/mm-reset-gyms /usr/local/bin/mm-reset-gyms
fi

# State dir (saves, daycare, neighbor cache) — pi-owned.
install -d -o pi -g pi /var/lib/monstermesh

# Save/ROM dirs the daemon watches.  The daemon's SaveWatcher fails to start
# if these don't exist at boot time and never retries, so creating them at
# bake time is the only reliable fix.  Standard RetroPie layout — also what
# EmulationStation expects for the gb system.
install -d -o pi -g pi /home/pi/RetroPie
install -d -o pi -g pi /home/pi/RetroPie/saves
install -d -o pi -g pi /home/pi/RetroPie/saves/gb
install -d -o pi -g pi /home/pi/RetroPie/roms
install -d -o pi -g pi /home/pi/RetroPie/roms/gb

systemctl enable monstermesh.service

# ── 4. Register MonsterMesh in EmulationStation ────────────────────────────
echo "[4/8] Registering EmulationStation system entry…"
ES_CFG=/etc/emulationstation/es_systems.cfg
ROM_DIR=/home/pi/RetroPie/roms/monstermesh
install -d -o pi -g pi "$ROM_DIR"
# ROMs for the MonsterMesh system.  Each .mm file is a launch marker; its name
# selects the experience in launch.sh.  "MonsterMesh Terminal" is the terminal;
# "Pentest Pikachu" boots straight into its battle screen.
sudo -u pi touch "${ROM_DIR}/MonsterMesh Terminal.mm"
sudo -u pi touch "${ROM_DIR}/Pentest Pikachu.mm"

if ! grep -q '<name>monstermesh</name>' "$ES_CFG" 2>/dev/null; then
    # Insert before the closing </systemList>.  Using python because sed-based
    # multi-line insertion is fragile across BSD/GNU sed.
    python3 - <<PY
import pathlib, re
p = pathlib.Path("${ES_CFG}")
text = p.read_text()
entry = pathlib.Path("${SRC}/retropie/es_systems_entry.xml").read_text().strip()
new = text.replace("</systemList>", entry + "\n</systemList>")
p.write_text(new)
PY
fi

# Theme drop-in (optional — falls back to default if missing).
if [ -d "${SRC}/retropie/themes/monstermesh" ]; then
    cp -r "${SRC}/retropie/themes/monstermesh" /etc/emulationstation/themes/ || true
fi

# Carbon theme tweaks for the small GPI screen: a readable ROM-list font and a
# tall stacked "Monster/Mesh" carousel logo.  Without a logo asset carbon shows
# the system name as huge truncated text.  We drop in a monstermesh.svg (the
# glyphs are baked to vector PATHS, since ES can't render SVG <text>), which
# only affects the monstermesh system — every other system keeps its own .svg.
for CARBON in /etc/emulationstation/themes/carbon*; do
    [ -d "$CARBON" ] || continue
    # Bigger ROM-list text (carbon's default 32 ≈ 14px once scaled to 480p).
    find "$CARBON" -name theme.xml -exec sed -i \
        's|<themeGamelistFontSize>32</themeGamelistFontSize>|<themeGamelistFontSize>56</themeGamelistFontSize>|' {} \;
    # MonsterMesh carousel logo (matches carbon's systems/<theme>.svg lookup).
    if [ -f "${SRC}/retropie/es-art/monstermesh.svg" ]; then
        install -d "$CARBON/art/systems"
        cp "${SRC}/retropie/es-art/monstermesh.svg" "$CARBON/art/systems/monstermesh.svg" || true
    fi
done

# ── 5. Python meshtastic relay venv ─────────────────────────────────────────
echo "[5/8] Setting up meshtastic Python venv…"
python3 -m venv /opt/monstermesh/.venv
# --no-cache-dir because the venv lives in the image and cache bloats it.
/opt/monstermesh/.venv/bin/pip install --no-cache-dir --upgrade pip
/opt/monstermesh/.venv/bin/pip install --no-cache-dir meshtastic

# ── 6. GPI Case 2W boot config overlays ────────────────────────────────────
echo "[6/8] Patching /boot/config.txt + overlays for GPI Case 2W…"
CFG=/boot/config.txt
PATCH="${SRC}/image-build/config-patches/config.txt.append"
OVERLAYS_SRC="${SRC}/image-build/config-patches/overlays"

# RetroPie 4.8's stock config.txt enables `dtoverlay=vc4-fkms-v3d` for
# fake-KMS hardware accel on Pi 4/5.  That overlay takes over the display
# controller and is incompatible with `dtoverlay=dpi24` — leave it in and
# the GPI's LCD never lights up.  Disable it before appending our overlays.
sed -i 's|^dtoverlay=vc4-fkms-v3d$|#dtoverlay=vc4-fkms-v3d  # disabled by Monster Nix: conflicts with dpi24 LCD|' "$CFG"
sed -i 's|^dtoverlay=vc4-kms-v3d$|#dtoverlay=vc4-kms-v3d  # disabled by Monster Nix: conflicts with dpi24 LCD|'  "$CFG"

# CRITICAL: replace stock dpi24.dtbo + pwm-audio-pi-zero.dtbo with RetroFlag's
# GPI-specific builds.  The stock overlays from raspberry-pi-overlays route
# pixel data to standard DPI pins; the GPI Case 2W's LCD ribbon is wired
# differently and reads from RetroFlag's custom pinout.  Without these the
# screen stays dark no matter what dpi_output_format you pick.  Source:
# https://github.com/RetroFlag/GPICASE2W-display-patch
echo "  Installing RetroFlag GPI Case 2W overlays…"
for ovl in dpi24.dtbo pwm-audio-pi-zero.dtbo; do
    if [ -f "${OVERLAYS_SRC}/${ovl}" ]; then
        # Back up the stock overlay (in case the user wants HDMI later).
        [ -f "/boot/overlays/${ovl}" ] && [ ! -f "/boot/overlays/${ovl}.stock" ] && \
            cp "/boot/overlays/${ovl}" "/boot/overlays/${ovl}.stock"
        cp -f "${OVERLAYS_SRC}/${ovl}" "/boot/overlays/${ovl}"
        echo "    installed /boot/overlays/${ovl} ($(stat -c %s "/boot/overlays/${ovl}") bytes)"
    else
        echo "    WARN: ${OVERLAYS_SRC}/${ovl} not found in source tree"
    fi
done

if ! grep -q '# Monster Nix / GPI Case 2W' "$CFG" 2>/dev/null; then
    {
        echo ""
        cat "$PATCH"
    } >> "$CFG"
fi

# ── 7. pi user → dialout ────────────────────────────────────────────────────
echo "[7/8] Adding pi to dialout (serial port access)…"
usermod -a -G dialout pi || true

# ── 8. (Optional) auto-launch MonsterMesh on boot ──────────────────────────
# Disabled by default — boot lands in EmulationStation, user picks MonsterMesh
# from the carousel.  Toggle by setting MONSTERMESH_AUTOLAUNCH=1 when running
# build.sh.  When on, we drop an autostart hook into ~pi/.emulationstation/.
echo "[8/8] Configuring auto-launch (${MONSTERMESH_AUTOLAUNCH:-0})…"
if [ "${MONSTERMESH_AUTOLAUNCH:-0}" = "1" ]; then
    install -d -o pi -g pi /home/pi/.emulationstation
    cat > /home/pi/.emulationstation/autostart.sh <<'AUTOSTART'
#!/bin/bash
# Auto-launch MonsterMesh on first ES start.
exec /opt/monstermesh/bin/launch.sh
AUTOSTART
    chmod +x /home/pi/.emulationstation/autostart.sh
    chown pi:pi /home/pi/.emulationstation/autostart.sh
fi

# ── 9. Strip build-time apt overrides from the final image ─────────────────
# The Debian-archive fallback was useful for the bake but we don't want the
# end user's Pi trying to pull from archive.debian.org at runtime.  Same for
# the expired-signature override.
echo "[9/9] Removing build-time apt overrides…"
rm -f /etc/apt/sources.list.d/debian-buster-archive.list
rm -f /etc/apt/apt.conf.d/99-monsternix-archive
# Clean apt's package cache so the image doesn't ship 100+ MB of cached .debs.
apt-get clean

echo "== customize.sh: done =="
