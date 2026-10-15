# CLAUDE.md

Guidance for AI-assisted work in this repository.

## What this is

Turn RFID tags into play buttons for Home Assistant. The repo ships the integration, the
firmware and a reference wiring; users build the physical device however they like (a kids'
music box is one example, not the premise). An ESP32 + RC522 reader POSTs `present` / `absent` /
`heartbeat` events to a Home Assistant webhook. The `jukeb` custom integration
owns everything else: tag registry, MP3 library, playback on a `media_player`,
removal behaviour, enabled toggle and active hours, the JukeB sidebar panel, and
a JukeB device with an online binary sensor and an enabled switch. The firmware
has no UI and no state.

## Layout

- `custom_components/jukeb/` the integration, at the repo root because HACS requires it
  (`hacs.json` beside it). `frontend/jukeb-panel.js` is the panel (vanilla JS, no build step).
  `brand/` holds the icon PNGs HA 2026.3+ and HACS show, rendered from `docs/images/icon.svg`
  by `tools/render_icon.py`; never edit the PNGs by hand.
  `home-assistant/` holds its README and the pytest suite (`pytest.ini` puts the repo root on the path).
- `firmware/` PlatformIO project. `src/` firmware, `include/config.example.h` the
  build-time config template (`include/config.h` is git-ignored), `test/` host tests.
- `docs/images/` pictures for the top-level README (banner, icon, `wiring.svg`, example drawings; photo placeholders listed in its README).
- `tools/` maintainer scripts (`render_icon.py`).

Every top-level folder has a README written for someone building the box; keep
them in sync with the code. Keep new top-level concerns in their own folder (e.g. `tools/`).
The repo holds no case or 3D models: users build the box into any enclosure.

## Build and test

Integration (Python 3.14, run from `home-assistant/`):
- `uv venv --python 3.14 .venv && uv pip install --python .venv/bin/python -r requirements_test.txt`
- `.venv/bin/python -m pytest -q` runs the suite against a real in-process HA core.
  `requirements_test.txt` pins the core to the minimum `homeassistant` in `hacs.json`; move both together.
- `node --check ../custom_components/jukeb/frontend/jukeb-panel.js` syntax-checks the panel.

Firmware (run from `firmware/`, PlatformIO at `~/.platformio/penv/bin/pio`):
- `pio test -e native` host tests for the pure modules.
- `pio run -e esp32dev` compile; `pio run -e esp32dev -t upload` flash; `pio device monitor`.

CI (`.github/workflows/`): `validate.yml` runs the HACS action and hassfest, `tests.yml` the
commands above. Both must stay green; HACS submission needs a release made after they pass.

## Releasing

Bump `VERSION` in `const.py` and `manifest.json`, merge to `master`, then push tag `vX.Y.Z` from
`master`. `release.yml` refuses a tag that differs from `manifest.json` (HACS shows the tag as the
version) and creates the GitHub release with generated notes and attaches `jukeb.zip`, which HACS installs
(`zip_release` in `hacs.json`; its download count tracks installs and updates); add a human summary on GitHub.

## Conventions

- `tests/conftest.py` imports `custom_components` first on purpose; do not reorder.
- Integration: all logic that does not touch HA services lives in `store.py`,
  `media.py`, `controller.py` and is tested directly. `webhook.py`, `websocket.py`,
  `upload.py`, `panel.py` are thin adapters. Validation errors are `StoreError` /
  `MediaError` with user-readable messages; websocket handlers map them to
  `send_error(id, "invalid", message)`.
- Websocket commands and the upload view are admin only. Global registrations
  (websocket, view, static path) happen once per HA run behind `DATA_SETUP_DONE`;
  the webhook, panel and entity platforms are registered per entry and removed on unload.
- Entities (`binary_sensor.py`, `switch.py`, base in `entity.py`) never poll: `JukeBData.touch`
  (box events, offline timer) and the store's settings listener send `SIGNAL_UPDATE` and the
  entities write state. New entities subclass `JukeBEntity` and read from `JukeBData`, set
  `_attr_translation_key` (name in `strings.json`, icon in `icons.json`) and never `_attr_name`.
  `translations/en.json` is a copy of `strings.json`; a test keeps them identical.
- Branding: the product is **JukeB** everywhere, in user-facing text (`JukeB`) and in
  identifiers (`jukeb` domain, URLs, websocket types, storage, `JUKEB_*` firmware macros).
- "Inactive" (enabled off or outside active hours) only suppresses playback for new objects;
  scans are still recorded and removal behaviour is unchanged. Decided 2026-10-13.
- Any change to `frontend/jukeb-panel.js` bumps `VERSION` in `const.py` and
  `manifest.json` together: the `?v=` query on the module URL is the only thing that
  makes browsers refetch the panel. `test_panel.py` pins both.
- Storage shape is fixed in `store.py` (`version 1`). Adding a media type means a
  new `media.type` value, never a new top-level key.
- Firmware: `PresenceDebouncer`, `EventPayload`, `ReplyParser` must not include
  Arduino headers (they build under `[env:native]`). Arduino modules wrap hardware.
  `main.cpp` only wires modules and owns the event queue.
- UIDs are upper-case hex bytes joined with `:` on both sides.
- Tests first (TDD): write the failing test, watch it fail, implement, watch it pass.

## Protocol

POST `<ha_url>/api/webhook/<webhook_id>`, JSON with `event`, `box_id`, `version`,
plus `uid` (present/absent) or `uptime_s` and `rssi` (heartbeat). Replies:
`{"ok":true,"known":bool}` for present, `{"ok":true}` otherwise, HTTP 400
`{"ok":false,"error":...}` for bad input. Changing playback behaviour is an
integration change, never a firmware change.
