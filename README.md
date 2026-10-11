# Jukebox

A music box for physical objects. Put an object with an RFID tag on the box and a
song plays on a Google Home / Chromecast speaker. Songs, speakers and behaviour are
configured from a web page served by the box itself.

```
[RC522] --SPI--> [ESP32 firmware] --HTTP POST JSON--> [Home Assistant webhook automation] --play_media--> [Cast speaker]
                       ^
                       | browser (LAN)
                  [Web UI on ESP32]
```

## Repository layout

| Folder | What |
|--------|------|
| `firmware/` | PlatformIO project for the ESP32 (C++). See `firmware/README.md`. |
| `homeassistant/` | The Home Assistant automation that turns webhook calls into playback. |
| `docs/` | Design specs and implementation plans. |

## Hardware

ESP32 dev board + RFID-RC522 on VSPI:

| RC522 | ESP32 GPIO |
|-------|-----------|
| SDA/SS | 5 |
| SCK | 18 |
| MOSI | 23 |
| MISO | 19 |
| RST | 21 |
| 3.3V / GND | 3.3V / GND |

## Quick start

1. Flash the firmware (the web UI is built into it): see `firmware/README.md`.
2. Join the `Jukebox-Setup` WiFi network, enter your WiFi credentials in the portal.
3. Install the automation from `homeassistant/jukebox_automation.yaml`.
4. Open `http://jukebox.local`, set the Home Assistant URL and your speaker entity IDs.
5. Scan an object, give it a name and a media URI, save. Scan it again to play.

Media URIs are whatever your Home Assistant media players accept, e.g.
`media-source://media_source/media/1.mp3` for files stored on Home Assistant.
