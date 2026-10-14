<p align="center">
  <img src="docs/images/banner.svg" alt="JukeB: put a toy on the box and its song plays. A tap-to-play RFID music box for kids, powered by Home Assistant." width="100%">
</p>

<p align="center">
  <a href="https://github.com/hacs/integration"><img src="https://img.shields.io/badge/HACS-Custom-41BDF5.svg" alt="HACS Custom"></a>
  <img src="https://img.shields.io/badge/Home%20Assistant-2026.2%2B-18BCF2?logo=homeassistant&logoColor=white" alt="Home Assistant 2026.2+">
  <img src="https://img.shields.io/badge/ESP32-RC522-E7352C?logo=espressif&logoColor=white" alt="ESP32 + RC522">
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-green.svg" alt="MIT License"></a>
</p>

# JukeB

**A Home Assistant integration and a tiny ESP32 box that turn toys into a music player for kids.**

Stick a cheap RFID tag on a toy. Put the toy on the box and its song plays on any speaker
Home Assistant controls. Lift it off and the music pauses.

- 🧸 **Made for small hands.** No screen, no buttons, no app. Toys are the remote.
- 🏠 **Runs in Home Assistant.** Install with HACS. Tags, songs and settings all live in a **Jukebox** page in the sidebar.
- 🔊 **Plays on the speakers you already have.** Google Nest, Cast, Sonos: anything that shows up in Home Assistant as a `media_player` and can play MP3s.
- 🌙 **Parent controls.** Set the volume, choose what lifting a toy does, set quiet hours, or switch the box off.
- 🔒 **Local only.** No cloud, no account. The box only talks to Home Assistant on your network.
- 🔧 **Cheap, common parts.** An ESP32, an RC522 reader and seven jumper wires.

<!-- 📷 the finished box on a shelf with a few tagged toys next to it. Save as `docs/images/hero.jpg` and replace this note with `![JukeB](docs/images/hero.jpg)`. -->

## How it works

```
  toy with an RFID tag
          │
          ▼
 ┌─────────────────┐   "tag 04:A3:7F:12 placed"   ┌──────────────────────────┐   play    ┌─────────┐
 │ JukeB box       │ ───────── Wi-Fi ───────────▶ │ Home Assistant           │ ────────▶ │ speaker │
 │ ESP32 + RC522   │ ◀────── "known tag" ──────── │ Jukebox integration      │           └─────────┘
 └─────────────────┘                              └──────────────────────────┘
```

The box only reports what it sees: a tag was placed, a tag was removed, and "I'm still here" every
30 seconds. Home Assistant does everything else: it knows which song goes with which tag, stores the
MP3s, and plays them. You never need to re-flash the box to add a song or change how it behaves.

<!-- 📷 the Jukebox panel in Home Assistant (Tags tab, a few tags listed). Save as `docs/images/panel.png`. -->

## What you need

| | Part | Notes |
|---|---|---|
| 🏠 | Home Assistant 2026.2 or newer | With [HACS](https://hacs.xyz) for the easy install |
| 🔊 | A speaker in Home Assistant | Any `media_player` that plays MP3s, e.g. a Google Nest Mini |
| 🧠 | ESP32 DevKit V1 (30-pin, ESP32-WROOM-32) | Other ESP32-WROOM boards work too. Not WROVER: it uses GPIO 16/17. |
| 📡 | RC522 RFID reader (13.56 MHz) | The common blue board. Some ship with loose header pins you'll need to solder on. |
| 🏷️ | 13.56 MHz RFID tags (MIFARE Classic or NTAG) | Stickers, coins or cards: one per toy |
| 🔌 | 7 female-to-female jumper wires | |
| 🔋 | Micro-USB **data** cable and a 5 V USB charger | Some cables only charge and can't flash the board |
| 📦 | Any box to put it in | Plastic or wood, not metal. See [step 5](#5-put-it-in-a-box). |

## Setup

### 1. Install the integration

**With HACS (recommended)**

[![Open your Home Assistant instance and open this repository in HACS.](https://my.home-assistant.io/badges/hacs_repository.svg)](https://my.home-assistant.io/redirect/hacs_repository/?owner=ToMeRhh&repository=JukeB&category=integration)

Click the button above, or do it by hand:

1. In Home Assistant, open **HACS**, click the **⋮** menu (top right) and choose **Custom repositories**.
2. Paste `https://github.com/ToMeRhh/JukeB`, set the type to **Integration** and click **Add**.
3. Search HACS for **Jukebox**, open it and click **Download**.
4. Restart Home Assistant.

**Without HACS**

Copy the [`custom_components/jukebox`](custom_components/jukebox) folder from this repository into
your Home Assistant `config/custom_components/` folder and restart Home Assistant.

**Then add it**

[![Open your Home Assistant instance and start setting up Jukebox.](https://my.home-assistant.io/badges/config_flow_start.svg)](https://my.home-assistant.io/redirect/config_flow_start/?domain=jukebox)

Or go to **Settings → Devices & services → Add integration** and pick **Jukebox**. It asks for two things:

- **Webhook ID**: the address the box sends to. Keep the default `jukebox` unless you have a reason
  to change it. You'll need it again in step 3.
- **Speaker**: where the music plays. You can change it later.

A **Jukebox** page now appears in the sidebar. It shows *Offline* until the box is running.

### 2. Wire the box

Seven wires connect the reader to the ESP32. The reader's eight pins line up with the bottom eight
pins of one side of the ESP32, in the same order, so the wires run straight across. Only GND and RST
swap at the end.

![Wiring diagram](docs/images/wiring.svg)

| RC522 | ESP32 |
|---|---|
| SDA (SS) | D5 (GPIO 5) |
| SCK | TX2 (GPIO 17) |
| MOSI | RX2 (GPIO 16) |
| MISO | D4 (GPIO 4) |
| IRQ | not connected |
| GND | GND |
| RST | D15 (GPIO 15) |
| 3.3V | 3V3 |

> [!WARNING]
> Power the RC522 from **3.3 V**, never 5 V. Pin labels differ between boards, so go by the GPIO number.

Wire it on the desk first. It's much easier to test before it goes into a box.

<!-- 📷 the ESP32 and RC522 wired together on the desk before going into the case. Save as `docs/images/wired.jpg`. -->

### 3. Flash the firmware

1. Install [PlatformIO](https://platformio.org/install) (the VS Code extension or `pipx install platformio`).
2. In the `firmware/` folder, copy `include/config.example.h` to `include/config.h` and fill in:
   - your Wi-Fi name and password (2.4 GHz: the ESP32 has no 5 GHz),
   - your Home Assistant address, e.g. `http://192.168.1.10:8123`,
   - the webhook ID from step 1.
3. Plug in the ESP32 and run:

   ```bash
   cd firmware
   pio run -e esp32dev -t upload
   ```

Within a few seconds the Jukebox page in Home Assistant switches to **Online**.
If it doesn't, see [firmware/README.md](firmware/README.md#serial-checklist) for the serial log
checklist and what the LED blinks mean.

### 4. Add songs and tags

In the **Jukebox** page:

1. **Media** tab: drag in your MP3 files.
2. Put a tagged toy on the box. The box blinks three times ("I don't know this one") and the
   **Tags** tab shows the tag under *Last scanned*.
3. Click **Use this UID**, name the toy, pick a song and click **Add tag**.
4. Lift the toy and put it back. The box blinks once and the song plays. 🎉

Repeat for every toy.

### 5. Put it in a box

There's no official case: use any box you like, bought, built or 3D-printed.

- The reader works through a few millimetres of plastic or wood, but not metal. Put the spot where
  the toy sits right above the reader, no more than about 5 mm away.
- Keep spare tags at least 3 cm from the reader, or it will read them instead.
- Leave a hole for the USB cable.

<!-- 📷 a child placing a toy on the box. Save as `docs/images/in-use.jpg`. -->

## Settings and automations

The **Settings** tab of the Jukebox page lets you:

- change the speaker and set a fixed volume,
- choose what happens when a toy is lifted: keep playing, pause or stop,
- set active hours, for example no music after bedtime.

The **Enabled** switch at the top turns the box off for a while.

Home Assistant also gets a **Jukebox** device for your automations and dashboards:

- `binary_sensor.jukebox_online`: whether the box is connected,
- `switch.jukebox_enabled`: the same on/off switch as in the Jukebox page.

More detail, troubleshooting and the update and uninstall steps are in
[home-assistant/README.md](home-assistant/README.md).

## Repository layout

| Folder | What's inside |
|---|---|
| [`custom_components/jukebox/`](custom_components/jukebox) | The Home Assistant integration and its sidebar page (this is what HACS installs) |
| [`home-assistant/`](home-assistant/) | Integration docs and tests |
| [`firmware/`](firmware/) | ESP32 firmware (PlatformIO, Arduino) and its tests |
| [`docs/`](docs/) | Banner, wiring diagram and photos |

## Development

- Integration tests: see [home-assistant/README.md](home-assistant/README.md#development).
- Firmware tests: `cd firmware && pio test -e native`.
- The box and Home Assistant talk over a single JSON webhook, documented in
  [home-assistant/README.md](home-assistant/README.md#protocol). Changes to what plays and when
  always go in the integration, never in the firmware.

Issues and pull requests are welcome.

## License

[MIT](LICENSE)
