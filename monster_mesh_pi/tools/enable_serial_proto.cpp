// enable_serial_proto.cpp
// Sends an AdminMessage over Meshtastic serial framing to enable the Serial
// plugin in PROTO mode on an nRF52840 Meshtastic node.
//
// Usage:  ./enable_serial_proto [port]
// Default port: /dev/tty.usbmodem1301
//
// Build:  g++ -std=c++17 -o enable_serial_proto enable_serial_proto.cpp
//
// Field numbers (from protobufs/meshtastic/):
//   ToRadio.packet             = field 1,  len-delim  tag=0x0A
//   MeshPacket.to              = field 3,  varint     tag=0x18
//   MeshPacket.decoded         = field 8,  len-delim  tag=0x42
//   MeshPacket.want_ack        = field 14, varint     tag=0x70
//   Data.portnum               = field 1,  varint     tag=0x08  (ADMIN_APP=68)
//   Data.payload               = field 2,  len-delim  tag=0x12
//   AdminMessage.set_module_config = field 35, len-delim  tag=[0x9A,0x02]
//   ModuleConfig.serial        = field 2,  len-delim  tag=0x12
//   SerialConfig.enabled       = field 1,  varint     tag=0x08
//   SerialConfig.mode          = field 7,  varint     tag=0x38  (PROTO=2)

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <time.h>

static constexpr uint8_t FRAME_START1 = 0x94;
static constexpr uint8_t FRAME_START2 = 0xC3;
static constexpr int ADMIN_APP_PORTNUM = 68;

// ── Minimal protobuf builder ──────────────────────────────────────────────────

static int writeVarint(uint8_t *out, int pos, uint64_t val) {
    do {
        uint8_t b = val & 0x7F;
        val >>= 7;
        if (val) b |= 0x80;
        out[pos++] = b;
    } while (val);
    return pos;
}

static int writeLenDelim(uint8_t *out, int pos, uint64_t tag,
                         const uint8_t *data, int dataLen) {
    pos = writeVarint(out, pos, tag);
    pos = writeVarint(out, pos, (uint64_t)dataLen);
    memcpy(out + pos, data, dataLen);
    return pos + dataLen;
}

// ── Packet builders ───────────────────────────────────────────────────────────

static int buildWantConfig(uint8_t *out) {
    // ToRadio { want_config_id: 1 }  field 100, varint
    // tag varint(800) = 0xA0 0x06, value = 0x01
    uint8_t proto[3] = {0xA0, 0x06, 0x01};
    out[0] = FRAME_START1; out[1] = FRAME_START2;
    out[2] = 0x00; out[3] = 0x03;
    memcpy(out + 4, proto, 3);
    return 7;
}

static int buildEnableSerialProto(uint8_t *out) {
    // SerialConfig { enabled=true, mode=PROTO(2) }
    uint8_t serialCfg[16];
    int sc = 0;
    sc = writeVarint(serialCfg, sc, 0x08); sc = writeVarint(serialCfg, sc, 1); // enabled=true
    sc = writeVarint(serialCfg, sc, 0x38); sc = writeVarint(serialCfg, sc, 2); // mode=PROTO

    // ModuleConfig { serial: SerialConfig }  serial=field 2, tag=0x12
    uint8_t modCfg[32];
    int mc = writeLenDelim(modCfg, 0, 0x12, serialCfg, sc);

    // AdminMessage { set_module_config: ModuleConfig }
    // set_module_config = field 35, len-delim
    // tag varint((35<<3)|2) = varint(282) = 0x9A 0x02
    uint8_t adminMsg[64];
    int am = 0;
    am = writeVarint(adminMsg, am, 0x9A02); // tag for field 35 len-delim... wait
    // Actually: (35 << 3) | 2 = 282
    // Need to encode 282 as varint in the tag write
    // Let me just hardcode the varint bytes
    // 282 = 256 + 26 = 0b100011010
    // 7-bit groups: 0011010 (26 = 0x1A), 0000010 (2 = 0x02)
    // With continuation: 0x9A, 0x02
    am = 0;
    adminMsg[am++] = 0x9A; adminMsg[am++] = 0x02; // varint tag for field 35 len-delim
    am = writeVarint(adminMsg, am, (uint64_t)mc);
    memcpy(adminMsg + am, modCfg, mc);
    am += mc;

    // Data { portnum=ADMIN_APP(68), payload=AdminMessage }
    uint8_t data[128];
    int dp = 0;
    dp = writeVarint(data, dp, 0x08); dp = writeVarint(data, dp, ADMIN_APP_PORTNUM);
    dp = writeLenDelim(data, dp, 0x12, adminMsg, am);

    // MeshPacket { to=0xFFFFFFFF, decoded=Data, want_ack=true }
    uint8_t pkt[192];
    int pp = 0;
    // to = 0xFFFFFFFF, field 3 varint, tag=0x18
    pp = writeVarint(pkt, pp, 0x18);
    pp = writeVarint(pkt, pp, 0xFFFFFFFF);
    // decoded = Data, field 8 len-delim, tag=0x42
    pp = writeLenDelim(pkt, pp, 0x42, data, dp);
    // want_ack = true, field 14 varint, tag=0x70
    pp = writeVarint(pkt, pp, 0x70); pp = writeVarint(pkt, pp, 1);

    // ToRadio { packet: MeshPacket }  field 1, len-delim, tag=0x0A
    uint8_t toRadio[220];
    int tr = writeLenDelim(toRadio, 0, 0x0A, pkt, pp);

    // Frame: 0x94 0xC3 len_hi len_lo [proto]
    out[0] = FRAME_START1; out[1] = FRAME_START2;
    out[2] = (tr >> 8) & 0xFF;
    out[3] =  tr       & 0xFF;
    memcpy(out + 4, toRadio, tr);
    return 4 + tr;
}

// ── Serial port ───────────────────────────────────────────────────────────────

static int openSerial(const char *port) {
    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror("open"); return -1; }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    tcgetattr(fd, &tty);
    cfmakeraw(&tty);
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);
    tty.c_cflag |= CS8 | CLOCAL | CREAD;
    tty.c_cflag &= ~(CRTSCTS | PARENB | CSTOPB);
    tty.c_cc[VMIN] = 0; tty.c_cc[VTIME] = 1;
    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}

static void msleep(int ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, nullptr);
}

static void dumpHex(const char *label, const uint8_t *buf, int len) {
    printf("%s (%d bytes):", label, len);
    for (int i = 0; i < len && i < 32; i++) printf(" %02X", buf[i]);
    if (len > 32) printf(" ...");
    printf("\n");
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    const char *port = (argc > 1) ? argv[1] : "/dev/tty.usbmodem1301";

    printf("=== Meshtastic Serial PROTO enabler ===\n");
    printf("Port: %s\n\n", port);

    int fd = openSerial(port);
    if (fd < 0) return 1;

    printf("Waiting 3s for device to boot...\n");
    msleep(3000);

    // Drain startup log
    {
        uint8_t junk[1024];
        int drained = 0;
        while (true) {
            int n = read(fd, junk, sizeof(junk));
            if (n <= 0) break;
            drained += n;
        }
        if (drained > 0) printf("Drained %d bytes of startup output\n", drained);
    }

    // Send want_config up to 5 times, waiting 1s each time for a response
    uint8_t buf[256];
    uint8_t rx[1024];
    bool gotFraming = false;

    for (int attempt = 1; attempt <= 5 && !gotFraming; attempt++) {
        int len = buildWantConfig(buf);
        printf("Attempt %d: sending want_config...\n", attempt);
        write(fd, buf, len);
        msleep(1500);

        int n = read(fd, rx, sizeof(rx));
        if (n > 0) {
            dumpHex("Response", rx, n);
            for (int i = 0; i < n-1; i++) {
                if (rx[i] == 0x94 && rx[i+1] == 0xC3) { gotFraming = true; break; }
            }
            printf("Meshtastic framing: %s\n\n", gotFraming ? "YES ✓" : "no (log text)");
        } else {
            printf("No response\n\n");
        }
    }

    if (!gotFraming) {
        printf("Device did not respond with Meshtastic framing after 5 attempts.\n");
        printf("This USB port appears to be debug-only on this device.\n\n");
    }

    // Send admin config: enable serial plugin PROTO mode
    {
    int len = buildEnableSerialProto(buf);
    dumpHex("Sending AdminMessage: serial.enabled=true, mode=PROTO", buf, len);
    write(fd, buf, len);
    msleep(1000);

    // Read response
    int n = read(fd, rx, sizeof(rx));
    if (n > 0) {
        dumpHex("Response", rx, n);
        bool hasFraming = false;
        for (int i = 0; i < n-1; i++) {
            if (rx[i] == 0x94 && rx[i+1] == 0xC3) { hasFraming = true; break; }
        }
        if (hasFraming) {
            printf("\nSUCCESS: Device responded with Meshtastic framing.\n");
            printf("The serial plugin should now be enabled in PROTO mode.\n");
            printf("Restart the MonsterMesh daemon to connect.\n");
        } else {
            printf("\nWARNING: Device still sending plain text.\n");
            printf("The admin packet may not have been processed.\n");
            printf("Try enabling via the Meshtastic mobile app instead:\n");
            printf("  Settings -> Module Config -> Serial -> Enabled=ON, Mode=PROTO\n");
        }
    } else {
        printf("No response to admin message.\n");
        printf("Try enabling via the Meshtastic mobile app:\n");
        printf("  Settings -> Module Config -> Serial -> Enabled=ON, Mode=PROTO\n");
    }
    } // end admin block

    close(fd);
    return 0;
}
