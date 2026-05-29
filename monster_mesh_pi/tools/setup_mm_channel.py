#!/usr/bin/env python3
"""
setup_mm_channel.py - Provision the "MonsterMesh" channel on a Meshtastic
                      node via USB.  Matches the auto-provision logic
                      ensureMonsterMeshChannel() runs on T-Decks:

  Channel name : "MonsterMesh"
  PSK          : ASCII "MonsterMesh!2024" (16 bytes, AES128)
  Role         : SECONDARY
  Index        : first free slot starting at 1
  Uplink/down  : enabled

Usage:
  ./setup_mm_channel.py /dev/cu.usbmodem1301
"""

import os
import sys
import time


def log(msg):
    print(f"[setup] {msg}", file=sys.stderr, flush=True)


# Re-exec into venv if needed
def _activate_venv_if_needed():
    try:
        import meshtastic  # noqa: F401
        return
    except ImportError:
        pass
    here = os.path.dirname(os.path.abspath(__file__))
    venv = os.path.join(here, ".venv")
    py   = os.path.join(venv, "bin", "python3")
    if os.path.exists(py):
        log(f"Re-exec into venv at {venv}")
        os.execv(py, [py] + sys.argv)


_activate_venv_if_needed()

import meshtastic.serial_interface
from meshtastic.protobuf import channel_pb2


MM_NAME = "MonsterMesh"
MM_PSK  = b"MonsterMesh!2024"   # 16 ASCII bytes = AES128 key

# Owner name shown in other nodes' NodeDB — T-Decks see this when
# challenging us via `mmb2 GPI`.  Keep shortName <= 4 chars.
OWNER_LONG  = "MonsterMesh Pi"
OWNER_SHORT = "GPI"


def main():
    if len(sys.argv) < 2:
        log("Usage: setup_mm_channel.py <serial_port>")
        sys.exit(1)
    port = sys.argv[1]

    # On macOS, translate /dev/tty.* -> /dev/cu.*
    if sys.platform == "darwin" and port.startswith("/dev/tty."):
        cu = port.replace("/dev/tty.", "/dev/cu.", 1)
        if os.path.exists(cu):
            port = cu

    log(f"Connecting to {port} ...")
    iface = meshtastic.serial_interface.SerialInterface(port)
    node  = iface.localNode

    # Wait for channel config to populate
    log("Waiting for channels ...")
    for _ in range(30):
        if node.channels:
            break
        time.sleep(0.5)
    if not node.channels:
        log("FATAL: node.channels empty after 15s")
        iface.close()
        sys.exit(1)

    # Find existing MonsterMesh channel or first free slot starting at index 1
    existing = -1
    free     = -1
    for i, c in enumerate(node.channels):
        name = c.settings.name if c.HasField("settings") else ""
        role = c.role
        if name == MM_NAME and role != channel_pb2.Channel.Role.DISABLED:
            existing = i
            break
        if free < 0 and i >= 1 and (
            role == channel_pb2.Channel.Role.DISABLED or not name
        ):
            free = i

    if existing >= 0:
        log(f"MonsterMesh channel already exists at index {existing}")
        # Update PSK to be sure it matches
        ch = node.channels[existing]
        ch.settings.psk = MM_PSK
        ch.settings.uplink_enabled   = True
        ch.settings.downlink_enabled = True
        node.writeChannel(existing)
        log("Updated PSK and uplink/downlink flags")
    elif free < 0:
        log("FATAL: no free channel slot (all 8 in use)")
        iface.close()
        sys.exit(1)
    else:
        ch = node.channels[free]
        ch.index = free
        ch.role  = channel_pb2.Channel.Role.SECONDARY
        ch.settings.name             = MM_NAME
        ch.settings.psk              = MM_PSK
        ch.settings.uplink_enabled   = True
        ch.settings.downlink_enabled = True
        # Some library versions require these to be cleared
        ch.settings.module_settings.position_precision = 0
        node.writeChannel(free)
        log(f"Added MonsterMesh channel at index {free}")

    # Dump final channel state for confirmation
    log("Final channels:")
    for i, c in enumerate(node.channels):
        if c.role == channel_pb2.Channel.Role.DISABLED:
            continue
        nm = c.settings.name if c.HasField("settings") else ""
        psk_len = len(c.settings.psk) if c.HasField("settings") else 0
        log(f"  [{i}] role={c.role} name='{nm}' psk_bytes={psk_len}")

    # Also set the owner name so T-Decks see "MonsterMesh Pi/GPI"
    # in their NodeDB rather than the default "Meshtastic XXXX".
    try:
        cur_long  = getattr(node, "owner", None)
        cur_long  = cur_long.long_name if cur_long else ""
        if cur_long != OWNER_LONG:
            log(f"Setting owner: longName='{OWNER_LONG}' shortName='{OWNER_SHORT}'")
            node.setOwner(OWNER_LONG, OWNER_SHORT)
        else:
            log(f"Owner already '{cur_long}', skipping")
    except Exception as e:
        log(f"setOwner failed (continuing): {e}")

    iface.close()
    log("Done. Restart the daemon to use the channel.")


if __name__ == "__main__":
    main()
