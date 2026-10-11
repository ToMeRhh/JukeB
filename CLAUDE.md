# CLAUDE.md

Guidance for AI-assisted work in this repository.

## What this is

An RFID music box: ESP32 + RC522 reader. A scanned tag makes the ESP32 POST a JSON
payload to a Home Assistant webhook; a Home Assistant automation plays the media on a
Cast speaker. Configuration lives on the ESP32 and is edited through its web UI.

Design spec: `docs/superpowers/specs/2026-10-11-rfid-jukebox-design.md`.
Implementation plan: `docs/superpowers/plans/2026-10-11-rfid-jukebox.md`.

## Layout

- `firmware/` PlatformIO project. `src/` firmware, `data/` files uploaded to LittleFS
  (the web UI), `test/` native unit tests.
- `homeassistant/` the webhook automation YAML and its README.
- `docs/` specs and plans.

Keep new top-level concerns in their own folder (e.g. `tools/`).

## Build and test

All commands run from `firmware/`. PlatformIO lives at `~/.platformio/penv/bin/pio`.

- `pio test -e native` runs the host unit tests (no board needed). Run this after every change to a pure-logic module.
- `pio run -e esp32dev` compiles the firmware.
- `pio run -e esp32dev -t buildfs` builds the LittleFS image from `data/`.
- `pio run -e esp32dev -t upload` / `-t uploadfs` flash firmware / filesystem.
- `pio device monitor` serial log at 115200.

## Conventions

- Pure-logic modules (`TagRegistry`, `Settings`, `HaPayload`, `PresenceTracker`) must
  not include Arduino or ESP-IDF headers. They are compiled for the host by the
  `native` environment (`build_src_filter` in `platformio.ini`) and unit-tested with
  Unity. Put new logic there first, test it there, then wire it into Arduino code.
- Arduino modules (`TagReader`, `Storage`, `HaClient`, `WifiSetup`, `WebApi`, `Led`)
  wrap hardware and network. `main.cpp` only wires modules together.
- Web handlers run on the async TCP task. They must not block (no HTTP calls, no
  delays). Mutate `AppState` under `state.mutex`; enqueue network work as a `Command`
  for `loop()`.
- JSON everywhere uses ArduinoJson 7 (`JsonDocument`). Keys are snake_case.
- UID strings are upper-case hex bytes joined with `:`.
- Tests: TDD. Write the failing Unity test, watch it fail, implement, watch it pass.

## Hardware pins

SS=5, SCK=18, MOSI=23, MISO=19 (VSPI defaults), RST=21 (held HIGH, software reset
only), onboard LED=2.

## Home Assistant contract

POST `<ha_url>/api/webhook/<webhook_id>` with JSON:
`{"event":"play"|"pause"|"stop"|"test", "uid", "name", "uri", "content_type", "player", "volume"?, "shuffle"}`.
Changing what Home Assistant does with it (e.g. moving to Music Assistant) is an
automation change, never a firmware change.
