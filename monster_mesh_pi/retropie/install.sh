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

# ── MQTT no-radio relay ──────────────────────────────────────────────────────
# The daemon auto-launches tools/mqtt_relay.py when no LoRa radio is attached,
# so the deck can talk to the mesh over MQTT alone. Install the script next to
# the binaries and provision a Python venv with its dependencies:
#   meshtastic        – protobuf definitions (ServiceEnvelope/MeshPacket/Data)
#   paho-mqtt         – MQTT client
#   pycryptodome      – channel AES-128-CTR
#   typing_extensions – paho dependency on Python 3.7 (Buster)
if [ -f "tools/mqtt_relay.py" ]; then
    sudo cp tools/mqtt_relay.py "$INSTALL_DIR/bin/mqtt_relay.py"
    sudo chmod +x "$INSTALL_DIR/bin/mqtt_relay.py"
fi
sudo apt-get install -y python3-venv python3-pip >/dev/null 2>&1 || \
    echo "  (note: could not apt-install python3-venv/pip — assuming present)"
VENV="$INSTALL_DIR/.venv"
if [ ! -x "$VENV/bin/python3" ]; then
    echo "Creating Python venv for the MQTT relay..."
    sudo python3 -m venv "$VENV"
fi
echo "Installing MQTT relay Python deps (may take a minute)..."
sudo "$VENV/bin/pip" install --upgrade pip >/dev/null 2>&1 || true
sudo "$VENV/bin/pip" install meshtastic paho-mqtt pycryptodome typing_extensions \
    || echo "  WARNING: MQTT deps failed to install — the no-radio relay won't run until these are present."

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

# Create the marker .mm files so the system shows in RetroPie. Each marker is a
# separate "game" in the MonsterMesh system; launch.sh dispatches on the name:
#   MonsterMesh.mm      -> the full terminal
#   Pentest Pikachu.mm  -> boots straight into the pentest battle (mmterm --pentest)
touch "$ROM_DIR/MonsterMesh.mm"
touch "$ROM_DIR/Pentest Pikachu.mm"

# Ensure pi user is in dialout for serial port access
sudo usermod -a -G dialout pi

echo ""
echo "Installation complete!"
echo "  - Daemon starts automatically on boot (systemd: monstermesh.service)"
echo "  - EmulationStation shows a MonsterMesh system with two entries:"
echo "      MonsterMesh (full terminal) and Pentest Pikachu"
echo "  - Mesh transport: attach a Meshtastic radio over USB, OR run with NO"
echo "    radio — the daemon falls back to MQTT automatically (mqtt_relay.py)."
echo "  - MQTT broker defaults to mqtt.cableclub.net. To point at another broker,"
echo "    create /etc/monstermesh/config.json with an \"mqtt\" block (host/port/"
echo "    user/pass/root/channel/psk/tls)."
echo ""
echo "To start now: sudo systemctl start monstermesh.service"
