# Jukebox firmware

ESP32 + RC522. Reports which tag is on the reader to the Home Assistant Jukebox
integration. It makes no decisions and stores nothing.

This folder is one part of JukeB; the [top-level README](../README.md) shows how
the pieces fit together. Install the [Home Assistant integration](../home-assistant/README.md)
first: you need its webhook ID here.

## What you need

- ESP32 DevKit V1 (30-pin, ESP32-WROOM-32) or another ESP32 dev board.
- RC522 RFID reader module and 13.56 MHz tags (MIFARE Classic, NTAG).
- 7 female-to-female jumper wires and a micro-USB **data** cable (some cables only charge).
- [PlatformIO](https://platformio.org/install): either the VS Code extension or the
  command line (`pipx install platformio` or `pip install platformio`).
- On Windows and macOS you may need the USB-serial driver for your board's chip
  (CP210x or CH340).

## Wiring

![Wiring diagram](../docs/images/wiring.svg)

The reader's eight header pins line up with the bottom eight pins of the ESP32
DevKit's right-hand row, in order. Only GND and RST swap places at the end.

| RC522 | ESP32 |
|---|---|
| SDA/SS | D5 (GPIO5) |
| SCK | TX2 (GPIO17) |
| MOSI | RX2 (GPIO16) |
| MISO | D4 (GPIO4) |
| IRQ | not connected (D2 sits opposite) |
| GND | GND |
| RST | D15 (GPIO15) |
| 3.3V | 3V3 |

Onboard LED on GPIO2. Power the RC522 from 3.3 V, never 5 V.

Use a board with an ESP32-WROOM module: on WROVER modules GPIO16/17 are
taken by PSRAM. Firmware before 2.1.0 used GPIO 18/19/21/23; rewire if
you update an older box.

## Configure

From this `firmware/` folder:

```bash
cp include/config.example.h include/config.h
```

Edit `include/config.h`: WiFi SSID and password, Home Assistant URL (use the IP
address if `homeassistant.local` does not resolve on your network), the webhook
ID you entered when adding the integration, and a box ID (any short name).
`config.h` is git-ignored.

```c
#define JUKEBOX_WIFI_SSID     "MyWifi"            // 2.4 GHz network; the ESP32 has no 5 GHz
#define JUKEBOX_WIFI_PASSWORD "secret"
#define JUKEBOX_HA_URL        "http://192.168.1.10:8123"  // no trailing slash
#define JUKEBOX_WEBHOOK_ID    "jukebox"           // same as in the integration
#define JUKEBOX_BOX_ID        "kids-room"         // shown in the panel
```

## Build, flash, watch

Plug the ESP32 in over USB, then from this `firmware/` folder:

```bash
pio run -e esp32dev -t upload   # build and flash (the first build downloads the toolchain)
pio device monitor              # watch the serial log at 115200 baud; Ctrl+C to quit
```

In VS Code, open the `firmware/` folder and use the PlatformIO toolbar's Upload and
Serial Monitor buttons instead.

If the upload stops at `Connecting....___`, hold the board's **BOOT** button until
writing starts. If no port is found, try another cable or install the USB driver.
If `pio` is not on your PATH after installing, it is usually at
`~/.platformio/penv/bin/pio`.

Host tests for the pure modules:

```bash
pio test -e native
```

## What the LED means

| pattern | meaning |
|---|---|
| slow blink (1 Hz) | WiFi down, retrying every 10 s |
| 1 blink | tag placed and the integration knows it |
| 3 blinks | tag placed, not registered yet (add it in the Jukebox panel) |
| 2 fast blinks | the event could not be delivered; see the serial log |

## Serial checklist

At 115200 baud you should see, in order:

1. `[boot] Jukebox firmware 2.1.0, box id ...` and the webhook URL.
2. `[reader] RC522 version 0x92 ok` (0x91/0x92/0xB2 are normal; `NOT FOUND` means wiring).
3. `[wifi] connected, ip ...`.
4. `[ha] heartbeat -> ok`. The Jukebox panel now shows **Online**.
   - `connection failed`: wrong URL/IP, or HA not reachable from this network.
   - `HTTP 200 but no Jukebox reply`: HA is reachable but the integration is not
     installed, or the webhook ID differs from `config.h`.
5. Place a tag: `[reader] present 04:A3:7F:12` then `[ha] present 04:A3:7F:12 -> ok, unknown tag`
   (3 blinks). The panel shows the UID under "Last scanned".
6. Register it in the panel, lift and place again: `-> ok, known` (1 blink) and music plays.
7. Lift it: `[reader] absent ...`, `[ha] absent ... -> ok`; after the grace period the
   speaker pauses (or stops / keeps playing, per Settings).

## Protocol

POST `<JUKEBOX_HA_URL>/api/webhook/<JUKEBOX_WEBHOOK_ID>` with JSON
`{"event":"present"|"absent"|"heartbeat","box_id","version","uid"?,"uptime_s"?,"rssi"?}`.
Events are queued (max 16, oldest dropped) and sent one per loop iteration while
WiFi is up. Heartbeats go out at boot and every 30 s.
