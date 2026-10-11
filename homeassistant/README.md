# Home Assistant side

The jukebox sends one JSON POST per event to
`http://<home-assistant>:8123/api/webhook/<webhook_id>`.

| event | fields |
|-------|--------|
| `play` | `uid`, `name`, `uri`, `content_type`, `player`, `volume` (optional, 0–1), `shuffle` |
| `pause` / `stop` | `uid`, `player` |
| `test` | none |

`player` is a `media_player` entity ID. `uri` and `content_type` are passed
straight to `media_player.play_media`, so anything your Cast player accepts
works, for example `media-source://media_source/media/1.mp3` with content type
`music`.

## Install

1. Settings → Automations & Scenes → Create automation → Create new automation.
2. Open the ⋮ menu → Edit in YAML, paste `jukebox_automation.yaml`, save.
3. In the jukebox web UI, set the Home Assistant URL and the webhook ID (`jukebox`
   unless you changed it in the YAML) and press **Test connection**. A
   "Jukebox connected" notification appears in Home Assistant.

The webhook has `local_only: true`, so only devices on your LAN can trigger it.

## Migrating to Music Assistant

When Music Assistant is installed with Spotify as a provider and Google Cast as a
player provider, replace the `media_player.play_media` step with:

```yaml
          - action: music_assistant.play_media
            target:
              entity_id: "{{ trigger.json.player }}"
            data:
              media_id: "{{ trigger.json.uri }}"
              media_type: "{{ trigger.json.content_type }}"
```

and in the jukebox UI point the tags at Spotify URIs (`spotify:playlist:...`,
content type `playlist` / `album` / `track`) and the players at the Music
Assistant `media_player` entities. Nothing changes in the firmware.
