#!/usr/bin/env python3
"""
mqtt_relay.py - bridges the MonsterMesh daemon to the MonsterMesh MQTT broker
                when there is NO Meshtastic radio attached.

This is a drop-in alternative to tools/mesh_relay.py. It speaks the exact same
stdio framing to the daemon, so MeshSerial::openRelay() can launch it without
any changes:

  relay -> daemon (incoming packets):
    [4B from_node BE] [2B payload_len BE] [payload bytes]
  daemon -> relay (outgoing packets):
    [4B dest_node BE] [2B payload_len BE] [payload bytes]
    dest_node = 0xFFFFFFFF -> broadcast (PRIVATE_APP)
    dest_node = 0xFFFFFFF2 -> broadcast TEXT_MESSAGE_APP on MonsterMesh channel

  Special frame on startup: from_node=0, len=4, payload=my_node_id (4B)
  Sentinel incoming from_node=0xFFFFFFF1 wraps a MonsterMesh-channel text
  message as [4B real_sender][text] (parity with mesh_relay.py).

Instead of a USB radio, this relay:
  * connects to the MonsterMesh MQTT broker (TLS),
  * subscribes to the Meshtastic encrypted topic for the MonsterMesh channel,
  * AES-128-CTR decrypts inbound ServiceEnvelopes -> Data -> PRIVATE_APP payload,
  * AES-128-CTR encrypts outbound MonsterMesh payloads into ServiceEnvelopes and
    publishes them under our own gateway topic.

The wire format matches Meshtastic's MQTT exactly so the Pi interoperates with
the T-Decks bridging the same channel.

Config (env var overrides > /etc/monstermesh/config.json > built-in defaults):
  MM_MQTT_HOST     default mqtt.cableclub.net
  MM_MQTT_PORT     default 8883   (TLS)
  MM_MQTT_USER     default ash
  MM_MQTT_PASS     default large4meowth
  MM_MQTT_ROOT     default kanto
  MM_MQTT_CHANNEL  default MonsterMesh
  MM_MQTT_PSK      default MonsterMesh!2024  (16 ASCII bytes = AES128 key)
  MM_MQTT_TLS      default 1  (1=TLS on port, 0=plaintext)
  MM_NODE_ID       optional explicit node id (hex like 0x1a2b3c4d or decimal);
                   otherwise derived stably from MAC/hostname (see node_id()).

The config.json (MMD_CONFIG_PATH) may carry an "mqtt" object with any of:
  { "mqtt": { "host": "...", "port": 8883, "user": "...", "pass": "...",
              "root": "kanto", "channel": "MonsterMesh", "psk": "MonsterMesh!2024",
              "tls": true, "node_id": "0x1a2b3c4d" } }
"""

import os
import sys
import json
import time
import struct
import socket
import hashlib
import threading

MMD_CONFIG_PATH          = "/etc/monstermesh/config.json"
PORTNUM_PRIVATE_APP      = 256
PORTNUM_TEXT_MESSAGE_APP = 1
PORTNUM_NODEINFO_APP     = 4

# Sentinels shared with mesh_relay.py / MeshSerial.
SENTINEL_MM_TEXT   = 0xFFFFFFF1   # incoming MM-channel TEXT_MESSAGE_APP wrapper
DEST_BROADCAST     = 0xFFFFFFFF   # outgoing PRIVATE_APP broadcast
DEST_MM_TEXT_BCAST = 0xFFFFFFF2   # outgoing TEXT_MESSAGE_APP broadcast on MM chan


def log(msg):
    print(f"[mqtt-relay] {msg}", file=sys.stderr, flush=True)


# ── Auto re-exec into the bundled venv if deps aren't importable ──────────────
def _activate_venv_if_needed():
    try:
        import paho.mqtt.client  # noqa: F401
        import meshtastic         # noqa: F401
        return
    except ImportError:
        pass
    here = os.path.dirname(os.path.abspath(__file__))
    # Search common venv locations: next to the script, one level up (the Pi
    # install lives at /opt/monstermesh/.venv while this script is installed to
    # /opt/monstermesh/bin/), and the well-known Pi install path. The env var
    # MM_VENV overrides everything.
    candidates = []
    if os.environ.get("MM_VENV"):
        candidates.append(os.environ["MM_VENV"])
    candidates += [
        os.path.join(here, ".venv"),
        os.path.join(here, "..", ".venv"),
        os.path.join(here, "..", "..", ".venv"),
        "/opt/monstermesh/.venv",
    ]
    for venv in candidates:
        py = os.path.abspath(os.path.join(venv, "bin", "python3"))
        # Compare the LITERAL path, not realpath: a venv's python3 is often a
        # symlink to /usr/bin/python3, but the venv's site-packages only load
        # when the interpreter is launched *via the venv path*. Guard against an
        # exec loop by checking we're not already running that exact path.
        if os.path.exists(py) and os.path.abspath(sys.executable) != py:
            log(f"Re-exec into venv at {os.path.abspath(venv)}")
            os.execv(py, [py] + sys.argv)
            return  # unreachable on success


_activate_venv_if_needed()

# ── Defensive imports — never crash the daemon; exit with a clear message ──────
try:
    import paho.mqtt.client as mqtt
except ImportError:
    log("FATAL: paho-mqtt not installed. Install with: pip install paho-mqtt")
    log("       (MonsterMesh daemon will keep running without a transport.)")
    sys.exit(3)

try:
    # meshtastic >= 2.3 nests protobufs under meshtastic.protobuf; older
    # releases (e.g. 2.2.x, which is what ships in the Pi venv) expose them at
    # the top level. Support both so the relay works on either.
    try:
        from meshtastic.protobuf import mqtt_pb2, mesh_pb2, portnums_pb2  # noqa: F401
    except ImportError:
        from meshtastic import mqtt_pb2, mesh_pb2, portnums_pb2  # noqa: F401
except ImportError:
    log("FATAL: meshtastic python package not installed. pip install meshtastic")
    sys.exit(3)

# AES-128-CTR: prefer 'cryptography', fall back to pycryptodome.
_AES_BACKEND = None
try:
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

    def aes_ctr(key: bytes, nonce16: bytes, data: bytes) -> bytes:
        c = Cipher(algorithms.AES(key), modes.CTR(nonce16))
        e = c.encryptor()
        return e.update(data) + e.finalize()

    _AES_BACKEND = "cryptography"
except ImportError:
    try:
        from Crypto.Cipher import AES
        from Crypto.Util import Counter

        def aes_ctr(key: bytes, nonce16: bytes, data: bytes) -> bytes:
            ctr = Counter.new(128, initial_value=int.from_bytes(nonce16, "big"))
            return AES.new(key, AES.MODE_CTR, counter=ctr).encrypt(data)

        _AES_BACKEND = "pycryptodome"
    except ImportError:
        log("FATAL: no AES backend. Install with: pip install cryptography")
        sys.exit(3)


# ── Config loading ────────────────────────────────────────────────────────────
def load_config():
    cfg = {
        "host":    "mqtt.cableclub.net",
        "port":    8883,
        "user":    "ash",
        "pass":    "large4meowth",
        "root":    "kanto",
        "channel": "MonsterMesh",
        "psk":     "MonsterMesh!2024",
        "tls":     True,
        "node_id": None,
    }
    # config.json overlays defaults (if present + parseable).
    try:
        if os.path.exists(MMD_CONFIG_PATH):
            with open(MMD_CONFIG_PATH) as f:
                j = json.load(f)
            m = j.get("mqtt", j) if isinstance(j, dict) else {}
            for k in ("host", "user", "pass", "root", "channel", "psk", "node_id"):
                if m.get(k) is not None:
                    cfg[k] = m[k]
            if m.get("port") is not None:
                cfg["port"] = int(m["port"])
            if m.get("tls") is not None:
                cfg["tls"] = bool(m["tls"])
            log(f"Loaded broker config from {MMD_CONFIG_PATH}")
    except Exception as e:
        log(f"config.json ignored ({e})")

    # Env vars win over everything.
    env = os.environ.get
    cfg["host"]    = env("MM_MQTT_HOST",    cfg["host"])
    cfg["user"]    = env("MM_MQTT_USER",    cfg["user"])
    cfg["pass"]    = env("MM_MQTT_PASS",    cfg["pass"])
    cfg["root"]    = env("MM_MQTT_ROOT",    cfg["root"])
    cfg["channel"] = env("MM_MQTT_CHANNEL", cfg["channel"])
    cfg["psk"]     = env("MM_MQTT_PSK",     cfg["psk"])
    if env("MM_MQTT_PORT"):
        cfg["port"] = int(env("MM_MQTT_PORT"))
    if env("MM_MQTT_TLS") is not None:
        cfg["tls"] = env("MM_MQTT_TLS") not in ("0", "false", "False", "")
    if env("MM_NODE_ID"):
        cfg["node_id"] = env("MM_NODE_ID")
    return cfg


# ── Node id: stable identity derived from MAC/hostname (no radio to ask) ───────
def node_id(explicit):
    if explicit is not None:
        try:
            s = str(explicit).strip()
            v = int(s, 16) if s.lower().startswith("0x") else int(s)
            return v & 0xFFFFFFFF
        except ValueError:
            log(f"MM_NODE_ID '{explicit}' not parseable — deriving instead")
    # Derive from the primary MAC when it's a real (globally-administered)
    # address; otherwise fall back to the hostname so it's still stable per-host.
    import uuid
    mac = uuid.getnode()
    locally_administered = (mac >> 40) & 0x02  # bit set => random/locally admin
    seed = mac.to_bytes(6, "big") if not locally_administered \
        else socket.gethostname().encode("utf-8")
    h = hashlib.sha256(b"monstermesh-pi:" + seed).digest()
    nid = int.from_bytes(h[:4], "big")
    # Avoid 0 and the broadcast id; keep it in the normal node-num space.
    if nid in (0, 0xFFFFFFFF):
        nid = 0x00A1B2C3
    return nid & 0xFFFFFFFF


# ── Meshtastic channel hash (Channels::hash / xorHash) ────────────────────────
def channel_hash(name: str, psk: bytes) -> int:
    def xor_bytes(b):
        code = 0
        for x in b:
            code ^= x
        return code
    return (xor_bytes(name.encode("utf-8")) ^ xor_bytes(psk)) & 0xFF


# ── Meshtastic CryptoEngine::initNonce ────────────────────────────────────────
#   nonce[0:8]  = packet_id  as uint64 little-endian
#   nonce[8:12] = from_node  as uint32 little-endian
#   nonce[12:16]= 0 (extra nonce, unused for channel encryption)
def make_nonce(packet_id: int, from_node: int) -> bytes:
    return (struct.pack("<Q", packet_id & 0xFFFFFFFFFFFFFFFF)
            + struct.pack("<I", from_node & 0xFFFFFFFF)
            + b"\x00\x00\x00\x00")


def main():
    # argv[1] (a serial port) is ignored — kept for openRelay() compatibility.
    cfg = load_config()
    psk = cfg["psk"].encode("utf-8") if isinstance(cfg["psk"], str) else cfg["psk"]
    if len(psk) not in (16, 32):
        log(f"WARNING: PSK is {len(psk)} bytes; expected 16 (AES128) or 32 (AES256)")

    my_node = node_id(cfg["node_id"])
    ch_name = cfg["channel"]
    ch_hash = channel_hash(ch_name, psk)
    gateway = f"!{my_node:08x}"

    root = cfg["root"].rstrip("/")
    pub_topic = f"{root}/2/e/{ch_name}/{gateway}"
    sub_topic = f"{root}/2/e/{ch_name}/#"

    log(f"AES backend: {_AES_BACKEND}")
    log(f"Broker: {cfg['host']}:{cfg['port']} tls={cfg['tls']} user={cfg['user']}")
    log(f"Node: 0x{my_node:08X} ({gateway})  channel='{ch_name}' hash=0x{ch_hash:02X}")
    log(f"Publish : {pub_topic}")
    log(f"Subscribe: {sub_topic}")

    # Tell the daemon our node id right away (special startup frame).
    sys.stdout.buffer.write(struct.pack(">IHI", 0, 4, my_node))
    sys.stdout.buffer.flush()
    stdout_lock = threading.Lock()

    def emit(from_node: int, payload: bytes):
        frame = struct.pack(">IH", from_node & 0xFFFFFFFF, len(payload)) + payload
        with stdout_lock:
            sys.stdout.buffer.write(frame)
            sys.stdout.buffer.flush()

    # ── MQTT client ───────────────────────────────────────────────────────────
    try:
        client = mqtt.Client(
            client_id=f"mmpi-{my_node:08x}",
            protocol=mqtt.MQTTv311,
            callback_api_version=mqtt.CallbackAPIVersion.VERSION1,
        )
    except (AttributeError, TypeError):
        # paho-mqtt < 2.0 has no callback_api_version kwarg.
        client = mqtt.Client(client_id=f"mmpi-{my_node:08x}")

    if cfg["user"]:
        client.username_pw_set(cfg["user"], cfg["pass"])
    if cfg["tls"]:
        client.tls_set()          # default system CA store
        client.tls_insecure_set(True)  # firmware uses setInsecure(); match it
    client.reconnect_delay_set(min_delay=1, max_delay=30)

    def on_connect(cl, userdata, flags, rc, *a):
        if rc == 0:
            log("MQTT connected")
            cl.subscribe(sub_topic, qos=0)
            log(f"Subscribed {sub_topic}")
        else:
            log(f"MQTT connect failed rc={rc}")

    def on_disconnect(cl, userdata, rc, *a):
        log(f"MQTT disconnected rc={rc} (auto-reconnecting)")

    def on_message(cl, userdata, msg):
        try:
            handle_envelope(msg.payload)
        except Exception as e:
            log(f"on_message error: {e}")

    def handle_envelope(raw: bytes):
        env = mqtt_pb2.ServiceEnvelope()
        try:
            env.ParseFromString(raw)
        except Exception:
            return  # not a ServiceEnvelope (e.g. a JSON/stat topic)
        # Ignore our own gateway echo and our own packets (self-echo desync).
        if env.gateway_id == gateway:
            return
        if env.channel_id and env.channel_id != ch_name:
            return
        pkt = env.packet
        from_node = getattr(pkt, "from")
        if from_node == my_node:
            return

        portnum, payload = decode_packet(pkt)
        if payload is None:
            return

        first = payload[0] if payload else 0
        log(f"RX from=0x{from_node:08X} portnum={portnum} bytes={len(payload)} "
            f"first=0x{first:02X}")

        if portnum == PORTNUM_PRIVATE_APP:
            emit(from_node, payload)
        elif portnum == PORTNUM_TEXT_MESSAGE_APP:
            # Wrap as [4B sender][text] behind the SENTINEL_MM_TEXT from-node,
            # exactly like mesh_relay.py, so the daemon routes it identically.
            wrapped = struct.pack(">I", from_node) + payload
            emit(SENTINEL_MM_TEXT, wrapped)
        # other portnums (NODEINFO etc.) are ignored for the daemon

    def decode_packet(pkt):
        """Return (portnum, payload_bytes) or (None, None)."""
        # Some bridges forward already-decoded packets.
        if pkt.HasField("decoded"):
            d = pkt.decoded
            return int(d.portnum), bytes(d.payload)
        if not pkt.encrypted:
            return None, None
        nonce = make_nonce(pkt.id, getattr(pkt, "from"))
        try:
            plain = aes_ctr(psk, nonce, bytes(pkt.encrypted))
            d = mesh_pb2.Data()
            d.ParseFromString(plain)
            return int(d.portnum), bytes(d.payload)
        except Exception as e:
            log(f"decrypt/parse failed: {e}")
            return None, None

    client.on_connect = on_connect
    client.on_disconnect = on_disconnect
    client.on_message = on_message

    # Connect with retry so a transient network/DNS blip doesn't kill the relay.
    connected = False
    for attempt in range(1, 6):
        try:
            client.connect(cfg["host"], cfg["port"], keepalive=60)
            connected = True
            break
        except Exception as e:
            log(f"connect attempt {attempt}/5 failed: {e}")
            time.sleep(min(2 * attempt, 10))
    if not connected:
        log("FATAL: could not reach broker after 5 attempts")
        sys.exit(4)

    client.loop_start()

    # ── Outgoing: daemon -> MQTT ───────────────────────────────────────────────
    seq_id = [int.from_bytes(os.urandom(4), "big") | 0x1]

    def next_packet_id():
        seq_id[0] = (seq_id[0] + 1) & 0xFFFFFFFF
        if seq_id[0] == 0:
            seq_id[0] = 1
        return seq_id[0]

    def publish_payload(dest_node: int, portnum: int, payload: bytes):
        data = mesh_pb2.Data()
        data.portnum = portnum
        data.payload = payload
        plain = data.SerializeToString()

        pkt_id = next_packet_id()
        nonce = make_nonce(pkt_id, my_node)
        enc = aes_ctr(psk, nonce, plain)

        pkt = mesh_pb2.MeshPacket()
        setattr(pkt, "from", my_node)
        pkt.to = dest_node & 0xFFFFFFFF
        pkt.id = pkt_id
        pkt.channel = ch_hash
        pkt.hop_limit = 3
        pkt.want_ack = False
        pkt.encrypted = enc

        env = mqtt_pb2.ServiceEnvelope()
        env.packet.CopyFrom(pkt)
        env.channel_id = ch_name
        env.gateway_id = gateway
        client.publish(pub_topic, env.SerializeToString(), qos=0)

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
                    log("daemon truncated payload")
                    os._exit(0)

                if dest_node == DEST_MM_TEXT_BCAST:
                    publish_payload(DEST_BROADCAST, PORTNUM_TEXT_MESSAGE_APP, payload)
                    log(f"tx TEXT {plen}B -> broadcast")
                else:
                    dest = DEST_BROADCAST if dest_node == DEST_BROADCAST else dest_node
                    publish_payload(dest, PORTNUM_PRIVATE_APP, payload)
                    log(f"tx {plen}B -> 0x{dest:08X}")

                # Beacon (first byte 0x60): also broadcast our NodeInfo so peers
                # populate their NodeDB with our long/short name (parity with
                # mesh_relay.py's NodeInfo tag-along).
                if payload and payload[0] == 0x60:
                    try:
                        user = mesh_pb2.User()
                        user.id = gateway
                        user.long_name = os.environ.get("MM_LONG_NAME", "MonsterMesh Pi")
                        user.short_name = os.environ.get("MM_SHORT_NAME", "GPI")
                        publish_payload(DEST_BROADCAST, PORTNUM_NODEINFO_APP,
                                        user.SerializeToString())
                        log("  + NodeInfo broadcast")
                    except Exception as e2:
                        log(f"  + NodeInfo failed: {e2}")
            except Exception as e:
                log(f"stdin_loop error: {e}")
                time.sleep(0.1)

    t = threading.Thread(target=stdin_loop, daemon=True)
    t.start()

    log("Ready - relaying between MQTT and daemon")
    try:
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        pass
    finally:
        try:
            client.loop_stop()
            client.disconnect()
        except Exception:
            pass


if __name__ == "__main__":
    main()
