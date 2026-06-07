#!/bin/bash
# MonsterMesh Terminal launcher for RetroPie / GPI Case 2W
DAEMON_BIN=/opt/monstermesh/bin/mmd
TERM_BIN=/opt/monstermesh/bin/mmterm
SOCK=/tmp/monstermesh.sock

# TERM=linux gives ncurses access to the framebuffer console's box-drawing
# character set.  Without this, ES launches us with TERM=dumb and borders
# render as 'qqqxxxlkmj' (ncurses ACS fallback).
export TERM=linux

# UTF-8 locale so ncurses (via setlocale(LC_ALL, "") in mmterm startup)
# knows it can emit multi-byte sequences for ACS box-drawing chars.
# C.UTF-8 is built into glibc and doesn't require locale-gen.
export LANG=C.UTF-8
export LC_ALL=C.UTF-8

# Force /dev/tty1 (the framebuffer console) into UTF-8 mode by writing the
# ISO-2022 escape ESC%G directly to it.  This is what `unicode_start`
# normally does but only on the current tty — our script's tty isn't tty1
# (we're called by EmulationStation), so we must target tty1 explicitly.
# Without this, the kernel renders each byte of our multi-byte UTF-8
# output as a separate glyph (e.g. ┌ → "M-b" or similar garbage).
sudo bash -c "printf '\033%%G' > /dev/tty1" 2>/dev/null

# Hide the framebuffer console's blinking text cursor.  ncurses calls
# curs_set(0) for its own cursor, but the kernel TTY's underline-block
# cursor is separate and shows through during slow redraws.  DEC private
# mode \e[?25l hides it; we restore on exit.
printf '\033[?25l' > /dev/tty1 2>/dev/null

# Boost console font on the 3" 640x480 LCD.  Default Fixed16 (8x16) gives
# 80x30 cells — readable but cramped.  Terminus24x12 (12x24) gives ~53x20
# cells — significantly bigger text, fits the menu/header layout (longest
# line is the header "GPI / ASH | Nbrs:N | LEAD Lv## [offline]" ~47 chars).
sudo setfont -O /tmp/.mm-prev-font 2>/dev/null
sudo setfont Lat15-Terminus24x12 2>/dev/null

# Restore on exit (handles ES coming back, Ctrl-C, anything).
cleanup() {
    printf '\033[?25h' > /dev/tty1 2>/dev/null
    [ -f /tmp/.mm-prev-font ] && sudo setfont /tmp/.mm-prev-font 2>/dev/null
    rm -f /tmp/.mm-prev-font
    # Leaving the console in UTF-8 mode is fine — RetroPie's default state
    # is UTF-8 anyway and ES handles either.
}
trap cleanup EXIT

# Start daemon if not running
if ! systemctl is-active --quiet monstermesh.service; then
    sudo systemctl start monstermesh.service
    sleep 2
fi

# Wait for socket
for i in $(seq 1 10); do
    [ -S "$SOCK" ] && break
    sleep 1
done

# Dispatch on which "ROM" was loaded from the MonsterMesh system.  The ROM is
# just a marker .mm file whose name selects the experience: the Pentest Pikachu
# ROM boots straight into its battle screen; anything else is the normal
# terminal.  ES passes the ROM path as $1 (see es_systems_entry.xml).
ROM_PATH="${1:-}"
case "$(basename "$ROM_PATH")" in
    *[Pp]entest*|*[Pp]ikachu*|*PENTEST*|*PIKACHU*)
        exec "$TERM_BIN" --pentest "$ROM_PATH" ;;
    *)
        exec "$TERM_BIN" ;;
esac
