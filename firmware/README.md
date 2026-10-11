# Jukebox firmware

ESP32 + RFID-RC522. PlatformIO project; Arduino core 3.x (pioarduino platform).

## Build, flash, test

PlatformIO CLI: `~/.platformio/penv/bin/pio` (or `pio` if on your PATH). Run from this folder.

| Command | What |
|---------|------|
| `pio test -e native` | host unit tests for the pure-logic modules |
| `pio run -e esp32dev` | compile firmware |
| `pio run -e esp32dev -t upload` | flash firmware (includes the web UI) |
| `pio device monitor` | serial log, 115200 baud |

The web UI (`data/index.html`) is embedded into the firmware image at build time,
so a normal `upload` is all you ever need. Tags and settings live on a LittleFS
partition that firmware uploads never touch, and it is formatted automatically on
first boot. Do **not** run `uploadfs`: it would erase your tags and settings.

## Wiring

| RC522 | ESP32 GPIO |
|-------|-----------|
| SDA/SS | 5 |
| SCK | 18 |
| MOSI | 23 |
| MISO | 19 |
| RST | 21 |
| 3.3V / GND | 3.3V / GND |

## First boot and on-device checklist

1. Serial shows `[reader] RC522 version 0x92 ok` (0x91 and 0x88 are fine too; `NOT FOUND` means wiring).
2. With no WiFi configured the box opens the open network `Jukebox-Setup`. Join it;
   the setup page opens (or browse to `http://192.168.4.1/`). The LED is solid on.
3. Enter SSID and password, **Save and reboot**. Serial shows `[wifi] connected, ip ...`.
4. `curl http://jukebox.local/api/status` returns JSON with `"mode":"sta"`.
5. Scan an unknown tag: LED blinks 3 times, the UID appears in the UI under "Last scanned tag".
6. Press **Use**, give it a name and a media URI, **Save tag**. Scan again: LED blinks once,
   the song plays, the status line shows `last HA call ok`.
7. Set "When the object is lifted" to Pause, save, lift the object: after the grace period
   the song pauses.
8. Power-cycle: tags and settings persist.

## Serial log tags

`[main]`, `[storage]`, `[reader]`, `[wifi]`, `[ha]`. A failed webhook call prints the
HTTP code or the transport error and makes the LED double-blink quickly.

## API

See the `WebApi` route table in `docs/superpowers/plans/2026-10-11-rfid-jukebox.md`
or read `src/WebApi.cpp`. Quick examples:

```bash
curl http://jukebox.local/api/status
curl http://jukebox.local/api/tags
curl -X PUT http://jukebox.local/api/tags/04:A3:7F:12 -H 'Content-Type: application/json' \
  -d '{"name":"Dino","uri":"media-source://media_source/media/1.mp3","player":"media_player.kitchen"}'
curl -X POST http://jukebox.local/api/tags/04:A3:7F:12/play
curl -X POST http://jukebox.local/api/test
```
