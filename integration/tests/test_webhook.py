import pytest
from pytest_homeassistant_custom_component.common import async_mock_service

from custom_components.jukebox.const import DOMAIN

URL = "/api/webhook/jukebox"


@pytest.fixture
def services(hass):
    return {
        name: async_mock_service(hass, "media_player", name)
        for name in ("volume_set", "play_media", "media_pause", "media_stop")
    }


@pytest.fixture
async def client(hass, setup_jukebox, hass_client_no_auth, services):
    hass.states.async_set("media_player.kitchen", "idle")
    data = hass.data[DOMAIN]
    await data.media.async_save("dino.mp3", b"x")
    await data.store.async_set_tag("AA:01", "Dino", {"type": "file", "path": "dino.mp3"}, data.media.exists)
    return await hass_client_no_auth()


async def post(client, body):
    resp = await client.post(URL, json=body)
    return resp.status, await resp.json()


async def test_present_known(hass, client, services):
    status, body = await post(client, {"event": "present", "uid": "aa:01", "box_id": "jukebox", "version": "2.0.0"})
    assert (status, body) == (200, {"ok": True, "known": True})
    await hass.async_block_till_done()
    assert len(services["play_media"]) == 1
    data = hass.data[DOMAIN]
    assert data.controller.current_uid == "AA:01"
    assert data.box.box_id == "jukebox" and data.box.version == "2.0.0"
    assert data.box.last_seen is not None


async def test_present_unknown(hass, client, services):
    status, body = await post(client, {"event": "present", "uid": "ZZ:99"})
    assert (status, body) == (200, {"ok": True, "known": False})
    await hass.async_block_till_done()
    assert services["play_media"] == []


async def test_absent_and_heartbeat(hass, client, services):
    await post(client, {"event": "present", "uid": "AA:01"})
    status, body = await post(client, {"event": "absent", "uid": "AA:01"})
    assert (status, body) == (200, {"ok": True})
    status, body = await post(client, {"event": "heartbeat", "uptime_s": 42, "rssi": -60, "version": "2.0.0"})
    assert (status, body) == (200, {"ok": True})
    box = hass.data[DOMAIN].box
    assert box.uptime_s == 42 and box.rssi == -60


async def test_invalid_json_is_400(client):
    resp = await client.post(URL, data=b"not json", headers={"Content-Type": "application/json"})
    assert resp.status == 400
    assert await resp.json() == {"ok": False, "error": "invalid json"}


async def test_non_object_json_is_400(client):
    for body in ([], "x", 7):
        status, reply = await post(client, body)
        assert (status, reply) == (400, {"ok": False, "error": "invalid json"})


async def test_unknown_event_is_400(client):
    status, body = await post(client, {"event": "dance"})
    assert (status, body) == (400, {"ok": False, "error": "unknown event"})
    status, body = await post(client, {"uid": "AA:01"})
    assert (status, body) == (400, {"ok": False, "error": "unknown event"})


async def test_missing_uid_is_400(hass, client):
    for event in ("present", "absent"):
        status, body = await post(client, {"event": event})
        assert (status, body) == (400, {"ok": False, "error": "uid required"})
        status, body = await post(client, {"event": event, "uid": "  "})
        assert (status, body) == (400, {"ok": False, "error": "uid required"})
    assert hass.data[DOMAIN].box.last_seen is None  # rejected events never count as contact


async def test_get_is_rejected(client):
    resp = await client.get(URL)
    assert resp.status == 405
