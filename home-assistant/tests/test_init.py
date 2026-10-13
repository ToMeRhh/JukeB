from homeassistant.config_entries import ConfigEntryState

from custom_components.jukebox.const import DOMAIN


async def test_setup_and_unload(hass, setup_jukebox, hass_media_dir, hass_client_no_auth):
    entry = setup_jukebox
    assert entry.state is ConfigEntryState.LOADED
    assert (hass_media_dir / "jukebox").is_dir()
    data = hass.data[DOMAIN]
    assert data.store.settings.player == "media_player.kitchen"

    assert await hass.config_entries.async_unload(entry.entry_id)
    await hass.async_block_till_done()
    assert entry.state is ConfigEntryState.NOT_LOADED
    assert DOMAIN not in hass.data
    client = await hass_client_no_auth()
    resp = await client.post("/api/webhook/jukebox", json={"event": "heartbeat"})
    assert resp.status == 200  # HA's generic reply for an unregistered webhook
    assert await resp.text() == ""
