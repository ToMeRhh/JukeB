# JukeB

A music box for kids: put an object on the box, its song plays on a Google
Cast speaker through Home Assistant.

```
 object with RFID tag                           Home Assistant
 ┌──────────────┐  present/absent/heartbeat   ┌──────────────────────┐
 │ ESP32 + RC522│ ───────── webhook ────────▶ │ Jukebox integration  │──▶ media_player
 │  (firmware/) │ ◀──── {"ok":true,"known"} ──│ + Jukebox panel (UI) │    (Cast speaker)
 └──────────────┘                             └──────────────────────┘
```

The box only reports. The integration owns tags, MP3 files, the speaker, and
what happens when an object is removed, all edited from the **Jukebox** sidebar
panel in Home Assistant.

## Repository layout

| Folder | What |
|---|---|
| `integration/` | Home Assistant custom integration (`custom_components/jukebox`) with its tests and README |
| `firmware/` | PlatformIO project for the ESP32 (Arduino core 3.x) with host tests and README |

## Quick start

1. **Integration**: copy `integration/custom_components/jukebox` into your HA
   `config/custom_components/`, restart, add the *Jukebox* integration (webhook ID + speaker).
   Details in `integration/README.md`.
2. **Firmware**: `cp firmware/include/config.example.h firmware/include/config.h`, fill in
   WiFi, HA URL and the same webhook ID, then from `firmware/` run
   `pio run -e esp32dev -t upload`. Details in `firmware/README.md`.
3. Open the **Jukebox** panel: it shows *Online* once the box's first heartbeat lands.
   Upload MP3s, scan an object, press *Use this UID*, name it, pick a file, save.

## Hardware

ESP32 dev board + RFID-RC522: SS→5, SCK→18, MOSI→23, MISO→19, RST→21, 3.3V, GND.

## Development

- Integration tests: `cd integration && .venv/bin/python -m pytest -q` (setup in its README).
- Firmware host tests: `cd firmware && pio test -e native`.
