#!/bin/bash
# MonsterMesh Pi Installer for RetroPie
# Run as pi user: bash install.sh

set -e

INSTALL_DIR=/opt/monstermesh
ES_SYSTEMS=/etc/emulationstation/es_systems.cfg
ROM_DIR=/home/pi/RetroPie/roms/monstermesh
SAVE_DIR=/home/pi/RetroPie/saves/gb
STATE_DIR=/var/lib/monstermesh

echo "=== MonsterMesh Pi Installer ==="

# Create directories
sudo mkdir -p "$INSTALL_DIR/bin"
sudo mkdir -p "$STATE_DIR"
sudo chown pi:pi "$STATE_DIR"
mkdir -p "$ROM_DIR"

# Copy binaries (must be built first with cmake + make)
if [ -f "build/mmd" ] && [ -f "build/mmterm" ]; then
    sudo cp build/mmd "$INSTALL_DIR/bin/mmd"
    sudo cp build/mmterm "$INSTALL_DIR/bin/mmterm"
    sudo chmod +x "$INSTALL_DIR/bin/mmd" "$INSTALL_DIR/bin/mmterm"
else
    echo "ERROR: build/mmd or build/mmterm not found. Run cmake + make first."
    exit 1
fi

# Copy launcher
sudo cp retropie/launch.sh "$INSTALL_DIR/bin/launch.sh"
sudo chmod +x "$INSTALL_DIR/bin/launch.sh"

# Install systemd service
sudo cp retropie/monstermesh.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable monstermesh.service

# Add to es_systems.cfg if not already there
if ! grep -q "monstermesh" "$ES_SYSTEMS" 2>/dev/null; then
    echo "Adding MonsterMesh to EmulationStation..."
    # Insert before </systemList>
    sudo sed -i 's|</systemList>|'"$(cat retropie/es_systems_entry.xml)"'\n</systemList>|' "$ES_SYSTEMS"
fi

# Install theme
if [ -d "retropie/themes/monstermesh" ]; then
    sudo cp -r retropie/themes/monstermesh /etc/emulationstation/themes/
fi

# Create a placeholder .mm file so the system shows in RetroPie
touch "$ROM_DIR/MonsterMesh.mm"

# Ensure pi user is in dialout for serial port access
sudo usermod -a -G dialout pi

echo ""
echo "Installation complete!"
echo "  - Daemon will start automatically on boot"
echo "  - Launch MonsterMesh from the RetroPie menu"
echo "  - Connect your nRF52 node to USB before playing"
echo ""
echo "To start now: sudo systemctl start monstermesh.service"
