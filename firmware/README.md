# Jukebox firmware

ESP32 + RC522. Reports which tag is on the reader to the Home Assistant Jukebox
integration. It makes no decisions and stores nothing.

## Wiring

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

Onboard LED on GPIO2. Use a board with an ESP32-WROOM module: on WROVER modules
GPIO16/17 are taken by PSRAM. Firmware before 2.1.0 used GPIO 18/19/21/23; rewire if
you update an older box.

## Configure

```bash
cp include/config.example.h include/config.h
```

Edit `include/config.h`: WiFi SSID and password, Home Assistant URL (use the IP
address if `homeassistant.local` does not resolve on your network), the webhook
ID you entered when adding the integration, and a box ID (any short name).
`config.h` is git-ignored.

## Build, flash, watch

PlatformIO lives at `~/.platformio/penv/bin/pio`.

```bash
pio run -e esp32dev -t upload
pio device monitor
```

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
