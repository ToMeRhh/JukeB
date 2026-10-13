# Jukebox integration for Home Assistant

Receives tag events from the Jukebox box (ESP32 + RC522), decides what to play,
and plays it on a Cast speaker. Everything is configured from the **Jukebox**
panel in the Home Assistant sidebar.

## Install

1. Copy `custom_components/jukebox` into your Home Assistant `config/custom_components/`
   folder (create `custom_components` if it does not exist). The result must be
   `config/custom_components/jukebox/manifest.json`.
2. Restart Home Assistant.
3. Settings → Devices & services → Add integration → **Jukebox**.
   - *Webhook ID*: must equal `JUKEBOX_WEBHOOK_ID` in the firmware's `config.h` (default `jukebox`).
   - *Speaker*: the `media_player` entity to play on. This is only the initial value;
     from then on the speaker is changed in the panel's Settings tab, not here.
4. Open **Jukebox** in the sidebar (admin users only).

The integration is a single-instance service: it adds no devices or entities.

## Panel guide

The status bar at the top is always visible: Online/Offline (offline after 90 s
without an event), last seen, RSSI, firmware version, what is playing or paused,
and the last playback error. Below it are three tabs.

- **Tags** — *Register an object* shows the last scanned UID. Tap an object on
  the box, press *Use this UID*, give it a name, pick a file, *Add tag*. The
  table lists every tag with Play, Edit and Delete.
- **Media** — drop MP3 files to upload (max 50 MB each; any file name works,
  including non-Latin scripts). Files are stored in `<media folder>/jukebox/` and
  play as `media-source://media_source/<media dir>/jukebox/<file>`. Each file
  has Play, which sends it to the speaker, and Pause while it is playing. Deleting
  a file that a tag uses is refused.
- **Settings** — speaker, an optional volume to set before each play (untick to
  leave the speaker's volume alone), what happens when the object is removed
  (keep playing / pause / stop) and the grace period that ignores brief lifts.

## Protocol

The box POSTs JSON to `/api/webhook/<webhook_id>` (local network only):

| event | fields | reply |
|---|---|---|
| `present` | `uid` | `{"ok":true,"known":true|false}` |
| `absent` | `uid` | `{"ok":true}` |
| `heartbeat` | `uptime_s`, `rssi` | `{"ok":true}` |

All events also carry `box_id` and `version`. Bad requests get HTTP 400 with
`{"ok":false,"error":"invalid json"|"unknown event"|"uid required"}`.

Try it without the box:

```bash
curl -s -X POST http://homeassistant.local:8123/api/webhook/jukebox \
  -H 'Content-Type: application/json' \
  -d '{"event":"present","uid":"04:A3:7F:12","box_id":"test","version":"manual"}'
```

The panel's status strip flips to Online and shows the UID under "Last scanned".

## Troubleshooting

- **Offline in the panel** — the box cannot reach HA. Check the serial log for
  `[ha] POST ... -> ...` lines; `connection failed` means wrong `JUKEBOX_HA_URL` or
  DNS (use the IP address instead of `homeassistant.local`).
- **Online, but a known tag does not play** — look at *Last error* in the status
  strip. `play_media on media_player.x failed` names the speaker and HA's reason.
- **HTTP 400 unknown event in the serial log** — firmware and integration versions
  disagree; flash the firmware from the same checkout.

## Development

```bash
cd integration
uv venv --python 3.13 .venv
uv pip install --python .venv/bin/python -r requirements_test.txt
.venv/bin/python -m pytest -q
```
