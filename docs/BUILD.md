# Building & flashing

MonsterMesh builds like any Meshtastic firmware, with [PlatformIO](https://platformio.org/). It only targets the **LilyGO T-Deck**.

## Prerequisites

- Python 3.x with a virtualenv
- PlatformIO (`pip install platformio`)
- A T-Deck, a USB-C cable, and a FAT32 micro-SD card with your own `.gb` ROM + `.sav`

## Build

```bash
# from the repo root
python3 -m venv .venv && source .venv/bin/activate
pip install platformio

pio run -e t-deck-tft
```

`t-deck-tft` is the environment that includes the LVGL device-ui (the themed UI MonsterMesh patches). The build defines `-D T_DECK`, which is what switches the MonsterMesh module on; without it the module compiles out entirely.

The output firmware is at `.pio/build/t-deck-tft/firmware.bin` (app image) and `.pio/build/t-deck-tft/firmware.factory.bin` (full image with bootloader + partition table).

### Clean rebuilds

If you hit stale-library or strange link errors, delete the build directory for the env and rebuild:

```bash
rm -rf .pio/build/t-deck-tft
pio run -e t-deck-tft
```

### Editing the themed UI (device-ui patches)

The files under `patches/device-ui/` are **copies** of files in the `meshtastic-device-ui` library. PlatformIO pulls a clean copy of that library into `.pio/libdeps/…`, so after editing a patch file you must copy it into **both** of the library's locations before rebuilding:

```
.pio/libdeps/t-deck-tft/meshtastic-device-ui/source/graphics/TFT/
.pio/libdeps/t-deck-tft/meshtastic-device-ui/generated/ui_320x240/
```

(There are project scripts that automate this copy; if in doubt, copy by hand and rebuild.)

## Flashing

### Over USB

```bash
pio run -e t-deck-tft -t upload --upload-port /dev/cu.usbmodemXXXX
```

The T-Deck enumerates as a USB-CDC serial port (often `/dev/cu.usbmodemXXXX`). After upload, power-cycle the device.

> **If a serial monitor/logger is attached, stop it first.** pyserial opens the port exclusively; an attached logger will make the flash silently fail or the port "busy."

### Over the air (M5 / bmorcelli-Launcher)

If the T-Deck is running an OTA launcher (e.g. bmorcelli-Launcher / M5Launcher) in WiFi mode, you can push a build over the network. The launcher's default credentials are `admin` / `launcher`:

```bash
FW=.pio/build/t-deck-tft/firmware.bin
SIZE=$(wc -c < "$FW")
IP=<device-ip>

curl -c /tmp/cookies.txt -d "username=admin&password=launcher" http://$IP/login -o /dev/null
curl -b /tmp/cookies.txt -F "command=0" -F "size=$SIZE" http://$IP/OTA
curl -b /tmp/cookies.txt -F "file1=@${FW};filename=firmware.bin-app.bin" http://$IP/OTAFILE --max-time 120
```

Reboot the device after the upload completes.

## ⚠️ Flashing safety

- **Never `esptool write_flash -e 0x0 …factory.bin` on a configured node.** Erasing from `0x0` with a factory image wipes the device's stored Meshtastic config — WiFi credentials, channel PSKs, and the node's private key. For a routine app update, flash the **app image only** at the app offset (`write_flash 0x10000 firmware.bin`) or use `pio run -t upload`, which does the right thing.
- Use the **factory** image only for first-time provisioning of a blank device (or when you intend to wipe it).
- Keep a backup of your SD-card `.sav` files; MonsterMesh writes daycare XP back into them.

## Pre-built images

The project keeps an archive of pre-built binaries (`firmware-builds/`), named like:

- `MonsterMesh-…-app.bin` — app-only image, flash at `0x10000` / via the OTA launcher
- `MonsterMesh-…-factory.bin` — full image for blank-device provisioning

Match `app` vs `factory` to the flashing method per the safety notes above.
