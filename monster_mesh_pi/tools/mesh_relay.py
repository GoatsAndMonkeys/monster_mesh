#!/usr/bin/env python3
"""
mesh_relay.py - bridges MonsterMesh daemon to a Meshtastic node via USB.

Uses the official meshtastic.serial_interface.SerialInterface. Confirmed
working: `meshtastic --port /dev/cu.usbmodemXXXX --info` returns full node
info from the WIO Tracker over USB.

Stdio framing (daemon <-> relay):
  relay -> daemon (incoming packets):
    [4B from_node BE] [2B payload_len BE] [payload bytes]
  daemon -> relay (outgoing packets):
    [4B dest_node BE] [2B payload_len BE] [payload bytes]
    dest_node = 0xFFFFFFFF -> broadcast

  Special frame on startup: from_node=0, len=4, payload=my_node_id
"""

import os
import sys
import time
import struct
import threading

PORTNUM_PRIVATE_APP       = 256
PORTNUM_TEXT_MESSAGE_APP  = 1
MONSTERMESH_CHANNEL       = 1
# Sentinel from_node values when relaying non-PRIVATE_APP messages so the
# daemon can route them. 0xFFFFFFF1 = MonsterMesh-channel text message.
SENTINEL_MM_TEXT          = 0xFFFFFFF1

def log(msg):
    print(f"[relay] {msg}", file=sys.stderr, flush=True)

# Auto-re-exec into venv if meshtastic isn't importable
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
from meshtastic.protobuf import portnums_pb2, mesh_pb2
from pubsub import pub


def main():
    if len(sys.argv) < 2:
        log("Usage: mesh_relay.py <serial_port>")
        sys.exit(1)
    port = sys.argv[1]

    # On macOS, /dev/tty.usbmodem* blocks on open() waiting for DCD.
    # /dev/cu.usbmodem* skips that wait.
    if sys.platform == "darwin" and port.startswith("/dev/tty."):
        cu_port = port.replace("/dev/tty.", "/dev/cu.", 1)
        if os.path.exists(cu_port):
            log(f"macOS: translating {port} -> {cu_port}")
            port = cu_port

    log(f"Connecting via USB serial: {port}")
    iface = None
    for attempt in range(3):
        try:
            iface = meshtastic.serial_interface.SerialInterface(port)
            break
        except Exception as e:
            log(f"Connect attempt {attempt+1}/3 failed: {e}")
            time.sleep(2)
    if iface is None:
        log(f"FATAL: could not connect to {port}")
        sys.exit(1)

    my_node = 0
    if iface.myInfo and hasattr(iface.myInfo, "my_node_num"):
        my_node = iface.myInfo.my_node_num
    log(f"Connected. Local node: 0x{my_node:08X}")

    # Send our node ID to the daemon as a special frame
    sys.stdout.buffer.write(struct.pack(">IHI", 0, 4, my_node))
    sys.stdout.buffer.flush()

    # ── Incoming: mesh -> daemon ──────────────────────────────────────────────
    def on_receive(packet, interface):
        try:
            decoded = packet.get("decoded") or {}
            portnum_raw = decoded.get("portnum", 0)
            portnum = portnum_raw
            if isinstance(portnum_raw, str):
                if "PRIVATE_APP" in portnum_raw:
                    portnum = PORTNUM_PRIVATE_APP
                elif "TEXT_MESSAGE_APP" in portnum_raw:
                    portnum = PORTNUM_TEXT_MESSAGE_APP
                else:
                    portnum = 0
            elif isinstance(portnum_raw, int):
                portnum = portnum_raw

            channel    = packet.get("channel", 0)
            from_node  = packet.get("from") or 0
            payload    = decoded.get("payload") or b""

            first_byte = payload[0] if payload else 0
            log(f"PKT ch={channel} portnum={portnum_raw} "
                f"from=0x{from_node:08X} bytes={len(payload)} "
                f"first=0x{first_byte:02X}")

            if not from_node:
                fid = packet.get("fromId") or ""
                if fid.startswith("!"):
                    try:
                        from_node = int(fid[1:], 16)
                    except ValueError:
                        from_node = 0

            # MonsterMesh-channel text messages carry daycare events and
            # achievement broadcasts. Wrap as [SENTINEL_MM_TEXT|4B sender|text]
            # so the daemon can route them through the same pipe.
            if (portnum == PORTNUM_TEXT_MESSAGE_APP
                    and channel == MONSTERMESH_CHANNEL
                    and payload):
                wrapped = struct.pack(">I", from_node) + payload
                frame = (struct.pack(">IH", SENTINEL_MM_TEXT, len(wrapped))
                         + wrapped)
                sys.stdout.buffer.write(frame)
                sys.stdout.buffer.flush()
                log(f"  -> forwarded MM-text {len(payload)}B from "
                    f"0x{from_node:08X}")
                return

            if portnum != PORTNUM_PRIVATE_APP:
                return  # not a MonsterMesh app payload
            if not payload:
                return

            frame = struct.pack(">IH", from_node, len(payload)) + payload
            sys.stdout.buffer.write(frame)
            sys.stdout.buffer.flush()
            log(f"  -> forwarded {len(payload)}B to daemon")
        except Exception as e:
            log(f"on_receive error: {e}")

    pub.subscribe(on_receive, "meshtastic.receive")

    # ── Outgoing: daemon -> mesh ──────────────────────────────────────────────
    def stdin_loop():
        while True:
            try:
                hdr = sys.stdin.buffer.read(6)
                if not hdr or len(hdr) < 6:
                    log("daemon closed stdin")
                    os._exit(0)
                dest_node, plen = struct.unpack(">IH", hdr)
                payload = sys.stdin.buffer.read(plen)
                if len(payload) < plen:
                    log("daemon truncated")
                    os._exit(0)
                # Sentinel destinations:
                #   0xFFFFFFFF -> PRIVATE_APP broadcast (default)
                #   0xFFFFFFF2 -> TEXT_MESSAGE_APP broadcast on MM channel
                #                 (used for fight-result chat announcements)
                if dest_node == 0xFFFFFFF2:
                    iface.sendData(
                        payload,
                        destinationId="^all",
                        portNum=portnums_pb2.PortNum.TEXT_MESSAGE_APP,
                        wantAck=False,
                        channelIndex=1,
                    )
                    log(f"tx TEXT {plen}B -> ^all ch=1")
                else:
                    dest_str = "^all" if dest_node == 0xFFFFFFFF else f"!{dest_node:08x}"
                    iface.sendData(
                        payload,
                        destinationId=dest_str,
                        portNum=portnums_pb2.PortNum.PRIVATE_APP,
                        wantAck=False,
                        channelIndex=1,
                    )
                    log(f"tx {plen}B -> {dest_str} ch=1")

                # If this was a DaycareBeacon (first byte 0x60), also broadcast
                # our NodeInfo on the same channel so peers populate their
                # NodeDB with our longName/shortName. Without this, T-Deck
                # users see "Meshtastic XXXX" when challenging us via mmb2.
                if payload and payload[0] == 0x60:
                    try:
                        own = iface.getMyUser() if hasattr(iface, "getMyUser") else None
                        user = mesh_pb2.User()
                        if own:
                            user.id         = own.get("id", f"!{my_node:08x}")
                            user.long_name  = own.get("longName", "")
                            user.short_name = own.get("shortName", "")
                            mac = own.get("macaddr", "")
                            if isinstance(mac, str) and len(mac) == 12:
                                user.macaddr = bytes.fromhex(mac)
                        else:
                            user.id         = f"!{my_node:08x}"
                            user.long_name  = "MonsterMesh Pi"
                            user.short_name = "GPI"
                        ni_payload = user.SerializeToString()
                        iface.sendData(
                            ni_payload,
                            destinationId="^all",
                            portNum=portnums_pb2.PortNum.NODEINFO_APP,
                            wantAck=False,
                            channelIndex=1,
                        )
                        log(f"  + NodeInfo {len(ni_payload)}B ch=1")
                    except Exception as e2:
                        log(f"  + NodeInfo broadcast failed: {e2}")
            except Exception as e:
                log(f"stdin_loop error: {e}")
                time.sleep(0.1)

    t = threading.Thread(target=stdin_loop, daemon=True)
    t.start()

    log("Ready - relaying between USB and daemon")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        iface.close()


if __name__ == "__main__":
    main()
