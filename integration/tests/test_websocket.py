import pytest
from pytest_homeassistant_custom_component.common import async_mock_service

from custom_components.jukebox.const import DOMAIN


@pytest.fixture
async def ws(hass, setup_jukebox, hass_ws_client):
    hass.states.async_set("media_player.kitchen", "idle")
    data = hass.data[DOMAIN]
    await data.media.async_save("dino.mp3", b"abc")
    await data.media.async_save("cat.mp3", b"de")
    return await hass_ws_client(hass)


async def call(ws, msg_type, **kwargs):
    call.counter = getattr(call, "counter", 0) + 1
    await ws.send_json({"id": call.counter, "type": msg_type, **kwargs})
    return await ws.receive_json()


async def test_status_initial(hass, ws):
    reply = await call(ws, "jukebox/status")
    assert reply["success"]
    r = reply["result"]
    assert r["online"] is False and r["last_seen"] is None
    assert r["current_uid"] is None and r["now_playing"] is None and r["last_error"] is None
    assert r["webhook_id"] == "jukebox"
    assert r["integration_version"] == "2.0.0"


async def test_status_after_events(hass, ws, hass_client_no_auth):
    async_mock_service(hass, "media_player", "volume_set")
    async_mock_service(hass, "media_player", "play_media")
    client = await hass_client_no_auth()
    await client.post("/api/webhook/jukebox", json={"event": "heartbeat", "rssi": -50, "uptime_s": 5, "version": "2.0.0", "box_id": "jukebox"})
    await client.post("/api/webhook/jukebox", json={"event": "present", "uid": "ZZ:99"})
    await hass.async_block_till_done()
    r = (await call(ws, "jukebox/status"))["result"]
    assert r["online"] is True and r["rssi"] == -50 and r["version"] == "2.0.0"
    assert r["last_scanned"]["uid"] == "ZZ:99" and r["last_scanned"]["known"] is False


async def test_tags_crud(hass, ws):
    reply = await call(ws, "jukebox/tags/set", uid="aa:01", name="Dino", media={"type": "file", "path": "dino.mp3"})
    assert reply["success"]
    assert reply["result"]["uid"] == "AA:01"
    assert reply["result"]["tag"]["name"] == "Dino"
    assert reply["result"]["tag"]["media"] == {"type": "file", "path": "dino.mp3"}

    reply = await call(ws, "jukebox/tags/list")
    assert list(reply["result"]["tags"]) == ["AA:01"]

    reply = await call(ws, "jukebox/tags/set", uid="AA:01", name="", media={"type": "file", "path": "dino.mp3"})
    assert not reply["success"]
    assert reply["error"] == {"code": "invalid", "message": "name is required"}

    reply = await call(ws, "jukebox/tags/set", uid="BB:02", name="x", media={"type": "file", "path": "missing.mp3"})
    assert reply["error"]["message"] == "file not found"

    reply = await call(ws, "jukebox/tags/delete", uid="AA:01")
    assert reply["success"]
    reply = await call(ws, "jukebox/tags/delete", uid="AA:01")
    assert reply["error"]["message"] == "unknown tag"
    assert (await call(ws, "jukebox/tags/list"))["result"]["tags"] == {}


async def test_play(hass, ws):
    play = async_mock_service(hass, "media_player", "play_media")
    async_mock_service(hass, "media_player", "volume_set")
    await call(ws, "jukebox/tags/set", uid="AA:01", name="Dino", media={"type": "file", "path": "dino.mp3"})
    reply = await call(ws, "jukebox/play", uid="AA:01")
    assert reply["success"] and reply["result"] == {}
    assert len(play) == 1
    reply = await call(ws, "jukebox/play", uid="ZZ:99")
    assert reply["error"]["message"] == "unknown tag"


async def test_settings(hass, ws):
    reply = await call(ws, "jukebox/settings/get")
    assert reply["result"]["settings"] == {
        "player": "media_player.kitchen",
        "volume": 0.3,
        "on_remove": "pause",
        "remove_grace_s": 2,
    }
    reply = await call(ws, "jukebox/settings/set", settings={"volume": 0.8, "on_remove": "stop"})
    assert reply["success"]
    assert reply["result"]["settings"]["volume"] == 0.8
    assert reply["result"]["settings"]["on_remove"] == "stop"
    assert reply["result"]["settings"]["player"] == "media_player.kitchen"
    reply = await call(ws, "jukebox/settings/set", settings={"player": "switch.x"})
    assert reply["error"]["message"] == "player must be a media_player entity"


async def test_media_list_and_delete(hass, ws):
    reply = await call(ws, "jukebox/media/list")
    assert reply["result"]["files"] == [{"path": "cat.mp3", "size": 2}, {"path": "dino.mp3", "size": 3}]
    reply = await call(ws, "jukebox/media/delete", path="cat.mp3")
    assert reply["success"]
    assert (await call(ws, "jukebox/media/list"))["result"]["files"] == [{"path": "dino.mp3", "size": 3}]
    reply = await call(ws, "jukebox/media/delete", path="../secret.mp3")
    assert reply["error"]["code"] == "invalid"


async def test_media_delete_refused_when_used(hass, ws):
    await call(ws, "jukebox/tags/set", uid="AA:01", name="Dino", media={"type": "file", "path": "dino.mp3"})
    reply = await call(ws, "jukebox/media/delete", path="dino.mp3")
    assert not reply["success"]
    assert reply["error"]["message"] == "file is used by tag Dino"
    assert hass.data[DOMAIN].media.exists("dino.mp3")


async def test_non_admin_rejected(hass, setup_jukebox, hass_ws_client, hass_read_only_access_token):
    ws = await hass_ws_client(hass, hass_read_only_access_token)
    for msg_type in ("jukebox/status", "jukebox/tags/list", "jukebox/settings/get", "jukebox/media/list"):
        reply = await call(ws, msg_type)
        assert not reply["success"] and reply["error"]["code"] == "unauthorized"
