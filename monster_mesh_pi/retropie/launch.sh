#!/bin/bash
# MonsterMesh Terminal launcher for RetroPie
# Checks if daemon is running, starts it if not, then launches the terminal UI

DAEMON_BIN=/opt/monstermesh/bin/mmd
TERM_BIN=/opt/monstermesh/bin/mmterm
SOCK=/tmp/monstermesh.sock

# Start daemon if not running
if ! systemctl is-active --quiet monstermesh.service; then
    echo "Starting MonsterMesh daemon..."
    sudo systemctl start monstermesh.service
    sleep 2
fi

# Wait for socket to appear (up to 10s)
for i in $(seq 1 10); do
    [ -S "$SOCK" ] && break
    sleep 1
done

# Launch terminal (inherits the display/TTY from EmulationStation)
exec "$TERM_BIN"
