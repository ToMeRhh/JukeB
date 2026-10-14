import json
from pathlib import Path

from homeassistant.helpers import device_registry as dr
from homeassistant.helpers import entity_registry as er
from homeassistant.util import dt as dt_util
from pytest_homeassistant_custom_component.common import async_fire_time_changed

from custom_components.jukebox.const import DOMAIN

ONLINE = "binary_sensor.jukebox_online"
ENABLED = "switch.jukebox_enabled"


async def heartbeat(hass, hass_client_no_auth, **extra):
    client = await hass_client_no_auth()
    payload = {"event": "heartbeat", "rssi": -55, "uptime_s": 10, "version": "2.2.0", "box_id": "jukebox", **extra}
    resp = await client.post("/api/webhook/jukebox", json=payload)
    assert resp.status == 200
    await hass.async_block_till_done()


async def test_entities_and_device_created(hass, setup_jukebox):
    assert hass.states.get(ONLINE).state == "off"
    assert hass.states.get(ONLINE).attributes["device_class"] == "connectivity"
    assert hass.states.get(ENABLED).state == "on"
    assert hass.states.get(ONLINE).attributes["friendly_name"] == "Jukebox Online"
    assert hass.states.get(ENABLED).attributes["friendly_name"] == "Jukebox Enabled"
    device = dr.async_get(hass).async_get_device(identifiers={(DOMAIN, setup_jukebox.entry_id)})
    assert device is not None and device.name == "Jukebox"
    assert device.sw_version is None


async def test_entities_use_translation_keys(hass, setup_jukebox):
    # Names come from translations/en.json and the switch icon from icons.json, both by key.
    registry = er.async_get(hass)
    assert registry.async_get(ONLINE).translation_key == "online"
    assert registry.async_get(ENABLED).translation_key == "enabled"


def test_icons_match_translations():
    integration = Path(__file__).resolve().parents[2] / "custom_components" / "jukebox"
    icons = json.loads((integration / "icons.json").read_text())["entity"]
    strings = json.loads((integration / "strings.json").read_text())["entity"]
    assert icons == {"switch": {"enabled": {"default": "mdi:music-note", "state": {"off": "mdi:music-note-off"}}}}
    for platform, keys in icons.items():
        assert set(keys) <= set(strings[platform])
    assert json.loads((integration / "translations" / "en.json").read_text()) == json.loads(
        (integration / "strings.json").read_text()
    )


async def test_online_follows_events_and_times_out(hass, setup_jukebox, hass_client_no_auth, freezer):
    await heartbeat(hass, hass_client_no_auth)
    state = hass.states.get(ONLINE)
    assert state.state == "on"
    assert state.attributes["rssi"] == -55 and state.attributes["box_id"] == "jukebox"
    device = dr.async_get(hass).async_get_device(identifiers={(DOMAIN, setup_jukebox.entry_id)})
    assert device.sw_version == "2.2.0"

    freezer.tick(60)
    async_fire_time_changed(hass)
    await hass.async_block_till_done()
    assert hass.states.get(ONLINE).state == "on"
    await heartbeat(hass, hass_client_no_auth)  # re-arms the timeout

    freezer.tick(60)
    async_fire_time_changed(hass)
    await hass.async_block_till_done()
    assert hass.states.get(ONLINE).state == "on"

    freezer.tick(31)
    async_fire_time_changed(hass)
    await hass.async_block_till_done()
    assert hass.states.get(ONLINE).state == "off"


async def test_switch_writes_store_and_follows_panel(hass, setup_jukebox, hass_ws_client):
    await hass.services.async_call("switch", "turn_off", {"entity_id": ENABLED}, blocking=True)
    assert hass.states.get(ENABLED).state == "off"
    data = hass.data[DOMAIN]
    assert data.store.settings.enabled is False
    assert data.controller.snapshot()["active"] is False

    ws = await hass_ws_client(hass)
    await ws.send_json({"id": 1, "type": "jukebox/settings/set", "settings": {"enabled": True}})
    assert (await ws.receive_json())["success"]
    await hass.async_block_till_done()
    assert hass.states.get(ENABLED).state == "on"

    await hass.services.async_call("switch", "turn_on", {"entity_id": ENABLED}, blocking=True)
    assert data.store.settings.enabled is True


async def test_unload_removes_entities(hass, setup_jukebox):
    assert await hass.config_entries.async_unload(setup_jukebox.entry_id)
    await hass.async_block_till_done()
    # HA keeps registered entities as "unavailable" until the entry loads again.
    assert hass.states.get(ONLINE).state == "unavailable"
    assert hass.states.get(ENABLED).state == "unavailable"
