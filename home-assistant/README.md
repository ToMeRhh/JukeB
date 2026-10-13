# Jukebox integration for Home Assistant

Receives tag events from the JukeB box (ESP32 + RC522), decides what to play,
and plays it on a speaker. Everything is configured from the **Jukebox**
panel in the Home Assistant sidebar.

This folder is one part of JukeB; the [top-level README](../README.md) shows how
the pieces fit together.

## Requirements

- Home Assistant 2026.x (the test suite runs against the 2026 release line).
- Access to the Home Assistant `config` folder (Samba, SSH, the File editor or
  Studio Code Server add-on, or a shell on the host).
- A `media_player` entity that can play MP3 files from Home Assistant's media
  folder, such as a Google Cast / Nest speaker.
- An admin user: the Jukebox panel and its API are admin only.
- The box must reach Home Assistant over the local network. The webhook only
  accepts requests from the local network.

## Install

1. Copy `custom_components/jukebox` from this folder into your Home Assistant
   `config/custom_components/` folder (create `custom_components` if it does not
   exist). The result must be `config/custom_components/jukebox/manifest.json`.

   From a checkout of this repository, on the Home Assistant host:

   ```bash
   cp -r home-assistant/custom_components/jukebox /config/custom_components/
   ```

2. Restart Home Assistant (Settings → System → Restart).
3. Settings → Devices & services → Add integration → **Jukebox**.
   - *Webhook ID*: any short secret-ish name. It must equal `JUKEBOX_WEBHOOK_ID`
     in the firmware's `config.h` (default `jukebox`).
   - *Speaker*: the `media_player` entity to play on. This is only the initial value;
     from then on the speaker is changed in the panel's Settings tab, not here.
4. Open **Jukebox** in the sidebar (admin users only). It shows *Offline* until
   the box sends its first event; [flash the firmware](../firmware/README.md) next.

The integration adds one **Jukebox** device with two entities for automations and
dashboards:

- `binary_sensor.jukebox_online` — on while the box has sent an event in the last 90 s;
  attributes carry RSSI, uptime, firmware version and last-seen time.
- `switch.jukebox_enabled` — the same Enabled toggle as the panel. Off means objects
  placed on the box are ignored (nothing starts; whatever is playing keeps its
  normal removal behaviour).

## Update

Replace `config/custom_components/jukebox` with the new version and restart Home
Assistant. Tags, settings and uploaded files are kept: they live in Home
Assistant's storage and media folder, not in the integration folder. If the
panel still looks old, reload the browser page.

## Uninstall

Remove the integration under Settings → Devices & services, delete
`config/custom_components/jukebox`, and restart. Your tags and settings stay in
`config/.storage/jukebox` and uploaded MP3s in `<media folder>/jukebox/`; delete
those too for a clean slate.

## Panel guide

The status bar at the top has a fixed layout: the Enabled toggle, Box (Online/Offline
and last seen), Signal, Firmware, Jukebox (Active, or Inactive with the reason: off or
outside the active hours), Playing, and an error line. Below it are three tabs.

- **Tags** — *Register an object* shows the last scanned UID. Tap an object on
  the box, press *Use this UID*, give it a name, pick a file, *Add tag*. The
  table lists every tag with Play (on the speaker), Edit and Delete. Scans are
  recorded even while the jukebox is inactive, so you can register tags any time.
- **Media** — drop MP3 files to upload (max 50 MB each; any file name works,
  including non-Latin scripts). Files are stored in `<media folder>/jukebox/` and
  play as `media-source://media_source/<media dir>/jukebox/<file>`. Play previews
  the file in your browser through the player above the list; the speaker is not
  involved. Deleting a file that a tag uses is refused.
- **Settings** — speaker, an optional volume to set before each play (untick to
  leave the speaker's volume alone), what happens when the object is removed
  (keep playing / pause / stop), the grace period that ignores brief lifts, and
  optional active hours (HA local time, may cross midnight, e.g. 19:00 to 07:00):
  outside the window objects are ignored. *Always* clears the window.

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
cd home-assistant
uv venv --python 3.13 .venv
uv pip install --python .venv/bin/python -r requirements_test.txt
.venv/bin/python -m pytest -q
```
