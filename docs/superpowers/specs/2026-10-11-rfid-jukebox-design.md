# RFID Jukebox: ESP32 + RC522 + Home Assistant — Design

Date: 2026-10-11

## 1. Goal

A music box for home use. Placing a physical object (with an RFID tag) on the box
starts a song on a chosen Google Home / Chromecast speaker. Songs, speakers and
behaviour are configured from a web UI served by the box itself.

Playback is delegated to Home Assistant (HA). Phase 1 plays files stored on the HA
container via `media-source://` URIs (already working on the user's HA). A later
phase moves to Music Assistant for Spotify; that change is confined to one HA
automation and does not touch the firmware.

## 2. Hardware

- ESP32 dev board (PlatformIO board `esp32dev`, Arduino core 3.x via the
  pioarduino `espressif32` platform already installed on the dev machine).
- RFID-RC522 on VSPI with default pins:

| RC522 | ESP32 GPIO |
|-------|-----------|
| SDA/SS | 5  |
| SCK    | 18 |
| MOSI   | 23 |
| MISO   | 19 |
| RST    | 21 |
| 3.3V / GND | 3.3V / GND |

- Onboard LED on GPIO 2 for feedback.

## 3. Architecture

```
[RC522] --SPI--> [ESP32 firmware] --HTTP POST JSON--> [HA webhook automation] --play_media--> [Cast speaker]
                       ^
                       | browser (LAN)
                  [Web UI on ESP32]
```

The ESP32 owns the tag-to-media mapping and all settings. On a scan it sends HA a
ready-made "play X on Y" request through a single webhook. HA owns nothing but
the automation that turns that request into `media_player` actions.

Security model: home LAN only. No login on the web UI. The HA webhook needs no
token, so no secrets live on the ESP32 except WiFi credentials. The HA webhook
keeps its default `local_only: true`.

## 4. Repository layout and firmware modules

The repository will hold more than the firmware later, so each part lives in its
own top-level folder:

```
README.md                     project overview, quick start, links
CLAUDE.md                     guidance for AI-assisted work in this repo
docs/superpowers/specs/       design specs
docs/superpowers/plans/       implementation plans
firmware/                     PlatformIO project (everything below is relative to it)
  platformio.ini
  src/main.cpp                wiring only: construct modules, call loop()
  src/TagReader.{h,cpp}       RC522 polling, presence debounce, placed/removed callbacks
  src/TagRegistry.{h,cpp}     pure logic: tag map <-> JSON, lookup, add/update/remove
  src/Settings.{h,cpp}        pure logic: settings struct <-> JSON, defaults, validation
  src/HaPayload.{h,cpp}       pure logic: builds the webhook JSON payload
  src/HaClient.{h,cpp}        POSTs a payload, one retry, reports result
  src/WebApi.{h,cpp}          HTTP routes; binds registry/settings/reader to the server
  src/WifiSetup.{h,cpp}       STA connect with timeout, else AP + captive portal
  data/index.html             single-file UI (vanilla JS, no build step) on LittleFS
  test/test_registry/         native unit tests
  test/test_settings/         native unit tests
  test/test_hapayload/        native unit tests
  README.md                   flashing, first setup, curl checklist
homeassistant/
  jukebox_automation.yaml     the webhook automation
  README.md                   how to install it, how to migrate to Music Assistant
```

Rule: `TagRegistry`, `Settings` and `HaPayload` must not
include any Arduino or ESP-IDF header. They take and return `std::string` /
plain structs so they compile and run under PlatformIO's `native` environment.
ArduinoJson is header-only and works natively, so it is allowed there.

### 4.1 Libraries (`platformio.ini`)

```
[env:esp32dev]
platform = espressif32            ; pioarduino fork, Arduino core 3.x
board = esp32dev
framework = arduino
monitor_speed = 115200
board_build.filesystem = littlefs
lib_deps =
  https://github.com/OSSLibraries/Arduino_MFRC522v2.git
  esp32async/ESPAsyncWebServer
  esp32async/AsyncTCP
  bblanchon/ArduinoJson@^7

[env:native]
platform = native
test_framework = unity
lib_deps = bblanchon/ArduinoJson@^7
build_src_filter = +<TagRegistry.cpp> +<Settings.cpp> +<HaPayload.cpp>
```

Exact pinned versions are chosen at implementation time from what resolves on the
dev machine (ESPAsyncWebServer 3.12.x, AsyncTCP 3.5.x, ArduinoJson 7.4.x were
current on 2026-10-11). HTTP client and LittleFS come from the Arduino core.

### 4.2 TagReader

- Uses MFRC522v2 with `MFRC522DriverPinSimple ss(5)` and `MFRC522DriverSPI`.
  RST pin 21 is driven by the library's default reset handling; SPI uses the VSPI
  defaults, which match the wiring table.
- The RC522 has no card-removed interrupt, so presence is polled every 100 ms:
  send WUPA (`PICC_WakeupA`, which wakes both IDLE and HALT cards), read the UID
  (`PICC_ReadCardSerial`), then `PICC_HaltA`. If any step fails, the poll counts
  as "no card".
- State machine per poll:
  - `ABSENT` and the same UID seen in 2 consecutive polls → `PRESENT`, emit
    `onPlaced(uid)`.
  - `PRESENT` and no card for longer than `pause_grace_ms` → `ABSENT`, emit
    `onRemoved(uid)`.
  - `PRESENT` and a different UID seen 2 consecutive polls → emit
    `onRemoved(old)` then `onPlaced(new)`.
- The UID is formatted as upper-case hex bytes joined by `:` (e.g. `04:A3:7F:12`).
  This string is the registry key.
- Runs in `loop()` with a millis-based schedule (no separate FreeRTOS task) to
  keep the SPI bus single-owner. Callbacks are invoked from `loop()`.

### 4.3 TagRegistry

Data model, stored at `/tags.json`:

```json
{
  "version": 1,
  "tags": {
    "04:A3:7F:12": {
      "name": "Blue dinosaur",
      "uri": "media-source://media_source/media/1.mp3",
      "content_type": "music",
      "player": "media_player.kitchen",
      "volume": 0.4,
      "shuffle": false
    }
  }
}
```

- `name` required, non-empty. `uri` required, non-empty.
- `content_type` defaults to `music`. Free string so later Spotify values
  (`playlist`, `album`, `track`) need no firmware change.
- `player` optional; empty means "use `default_player` from settings".
- `volume` optional. Absent or `null` means "use `default_volume`". `-1` means
  "don't touch the speaker's volume" and is stored as `-1`. Otherwise 0.0–1.0.
- `shuffle` defaults to `false`.
- API: `load(json) -> bool`, `save() -> json`, `find(uid) -> optional<Tag>`,
  `upsert(uid, Tag)`, `remove(uid) -> bool`, `all() -> vector<pair<uid,Tag>>`.
- Max 200 tags (guard on `upsert`; the UI shows the count).

### 4.4 Settings

Stored at `/settings.json`:

```json
{
  "version": 1,
  "wifi_ssid": "", "wifi_pass": "",
  "hostname": "jukebox",
  "ha_url": "http://homeassistant.local:8123",
  "webhook_id": "jukebox",
  "players": ["media_player.kitchen", "media_player.living_room"],
  "default_player": "media_player.kitchen",
  "default_volume": 0.3,
  "on_remove": "none",
  "pause_grace_ms": 1500
}
```

- `on_remove` ∈ {`none`, `pause`, `stop`}. Invalid values fall back to `none`.
- `pause_grace_ms` clamped to 200–30000.
- `default_volume` clamped to 0.0–1.0.
- `ha_url`: scheme + host [+ port], trailing slash stripped.
- `players` is the list the UI offers in dropdowns; `default_player` must be one
  of them or empty.
- WiFi password is never returned by the API (masked as `"********"`; a masked
  value on write means "unchanged").

### 4.5 HaClient

- `HaPayload::build(event, uid, tag, settings) -> std::string` (native-testable):

```json
{"event":"play","uid":"04:A3:7F:12","name":"Blue dinosaur",
 "uri":"media-source://media_source/media/1.mp3","content_type":"music",
 "player":"media_player.kitchen","volume":0.4,"shuffle":false}
```
  `event` ∈ {`play`, `pause`, `stop`}. For `pause`/`stop` only `event`, `uid`,
  `player` are sent. `volume` is omitted when "don't touch".
- `HaClient::send(payload)` POSTs to `<ha_url>/api/webhook/<webhook_id>` with
  `Content-Type: application/json`, 5 s timeout. HA answers 200 for an accepted
  webhook. Anything else, or a transport error, is retried once after 500 ms.
  Result (`ok`, HTTP code, message, timestamp) is stored as `lastResult` for the
  UI.
- Called from `loop()` (blocking for up to ~11 s worst case). The reader keeps
  its state machine time-based so a slow POST does not generate phantom removals:
  the removal timer is only evaluated on polls that actually ran.

### 4.6 Behaviour on events

- `onPlaced(uid)`: record `lastScanned = {uid, millis}`. If the UID is known →
  LED one blink, send `play`. If unknown → LED three blinks, nothing sent.
- `onRemoved(uid)`: if `on_remove != none` and the UID is known → send
  `pause` or `stop` to the tag's effective player. (Grace period is already
  applied by the reader, so a brief lift does nothing.)
- Placing the same known tag again after removal starts the song again from
  the beginning (HA `play_media` semantics). This is intentional.

### 4.7 WifiSetup

- On boot: if `wifi_ssid` is empty → setup mode. Else try STA for 20 s.
- Setup mode: AP `Jukebox-Setup` (open), IP 192.168.4.1, DNS captive portal
  that redirects any host to the setup page (the same `index.html`, which
  detects AP mode via `/api/status` and shows the WiFi card first). LED solid on.
- In STA mode: mDNS `jukebox.local`; LED off when idle.
- If STA drops, the core's auto-reconnect is used; if there is no connection
  for 60 s, fall back to setup mode without touching saved data. A later
  successful save of WiFi settings reboots into STA.

### 4.8 WebApi

All JSON. Static `index.html` served from LittleFS at `/`.

| Method | Path | Purpose |
|-------|------|---------|
| GET | `/api/status` | mode (`sta`/`ap`), IP, RSSI, uptime, free heap, `lastScanned`, `lastResult`, tag count |
| GET | `/api/tags` | full registry |
| PUT | `/api/tags/{uid}` | create or replace one tag (body = tag object) |
| DELETE | `/api/tags/{uid}` | remove |
| POST | `/api/tags/{uid}/play` | send `play` for this tag now (test button) |
| GET | `/api/settings` | settings with masked WiFi password |
| PUT | `/api/settings` | replace settings; validates; persists; returns the stored result |
| POST | `/api/test` | POST a `{"event":"test"}` payload to the webhook; returns HTTP code |
| POST | `/api/reboot` | reboot |

Errors: 400 with `{"error":"..."}` on validation failures, 404 for unknown UID,
507 if the registry is full. All writes are persisted to LittleFS before the
response is sent.

### 4.9 Web UI (`data/index.html`)

Single page, no framework, no external assets (works in AP mode without
internet). Three cards:

1. **Tags.** Table: name, URI, player, volume, shuffle, with Edit, Delete and
   "Play now". Edit opens the same form as Add, pre-filled.
2. **Add tag.** Shows the last scanned UID (polled from `/api/status` every
   second, highlighted when it changes). Fields: name, URI, content type
   (default `music`), player (dropdown from `settings.players`, plus "default"),
   volume (slider, with "use default" checkbox), shuffle. Save → `PUT /api/tags/{uid}`.
3. **Settings.** HA URL, webhook ID, players list (one per line), default
   player, default volume, on-remove behaviour, grace period, hostname, WiFi
   SSID/password. Buttons: Save, Test connection, Reboot. A status strip shows
   mode, IP, last HA result and last error.

## 5. Home Assistant side (`homeassistant/jukebox_automation.yaml`)

One automation, webhook trigger `jukebox`, `allowed_methods: [POST]`,
`local_only: true`. It branches on `trigger.json.event`:

- `play`:
  1. if `trigger.json.volume` is defined → `media_player.volume_set` on
     `trigger.json.player`.
  2. `media_player.play_media` on `trigger.json.player` with
     `media_content_id: "{{ trigger.json.uri }}"`,
     `media_content_type: "{{ trigger.json.content_type }}"`.
  3. if `trigger.json.shuffle` → `media_player.shuffle_set` (ignored by
     players that don't support it).
- `pause` → `media_player.media_pause`; `stop` → `media_player.media_stop`.
- `test` → `persistent_notification.create` "Jukebox connected".

Migration to Music Assistant later: replace step 2 with
`music_assistant.play_media` targeting the MA player entity with
`media_id: trigger.json.uri`, `media_type: trigger.json.content_type`. No
firmware change.

## 6. Error handling

- Corrupt or unparsable `tags.json` / `settings.json`: file is renamed to
  `*.bad`, defaults are used, and the UI status strip shows a warning until the
  next successful save. The box never fails to boot because of config.
- HA unreachable: one retry, then `lastResult` records the failure. The LED
  gives a fast double blink on send failure.
- Tag placed while WiFi is down: nothing sent; `lastScanned` still updates so
  the UI (if on AP) can still register tags.
- Reader not responding at boot (version register reads 0x00/0xFF): status
  shows "reader not found", everything else keeps working.
- Web API input: size-limited bodies (8 KB), ArduinoJson parse errors → 400.

## 7. Testing

- **Native unit tests** (`pio test -e native`), run on the dev machine:
  - TagRegistry: load/save round-trip, defaults, validation, upsert/remove,
    capacity guard, malformed JSON rejected.
  - Settings: defaults, clamping, `on_remove` validation, URL normalisation,
    password masking/unchanged semantics.
  - HaPayload: exact JSON for play with/without volume, pause, stop, test.
- **Build check**: `pio run -e esp32dev` and `pio run -e esp32dev -t buildfs`
  must succeed on the dev machine.
- **On-device checklist** (README; the user flashes and reports back):
  1. Serial shows RC522 firmware version.
  2. AP mode appears, captive portal opens, WiFi saved, box joins LAN.
  3. `curl http://jukebox.local/api/status` returns JSON.
  4. Scan an unknown tag → three blinks, UID appears in the UI.
  5. Register it, scan again → song plays on the chosen speaker.
  6. Set on-remove to pause, lift the object → pauses after grace period.
  7. Power-cycle → tags and settings persist.

## 8. Out of scope (v1)

Web UI authentication, OTA updates, multiple readers, HA entity discovery,
HTTPS to HA, writing data to tags (UID only is used).
