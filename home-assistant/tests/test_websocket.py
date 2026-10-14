import pytest
from pytest_homeassistant_custom_component.common import async_mock_service

from custom_components.jukeb.const import DOMAIN


@pytest.fixture
async def ws(hass, setup_jukeb, hass_ws_client):
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
    reply = await call(ws, "jukeb/status")
    assert reply["success"]
    r = reply["result"]
    assert r["online"] is False and r["last_seen"] is None
    assert r["current_uid"] is None and r["now_playing"] is None and r["last_error"] is None
    assert r["webhook_id"] == "jukeb"
    assert r["integration_version"] == "3.0.0"


async def test_status_after_events(hass, ws, hass_client_no_auth):
    async_mock_service(hass, "media_player", "volume_set")
    async_mock_service(hass, "media_player", "play_media")
    client = await hass_client_no_auth()
    await client.post("/api/webhook/jukeb", json={"event": "heartbeat", "rssi": -50, "uptime_s": 5, "version": "2.0.0", "box_id": "jukeb"})
    await client.post("/api/webhook/jukeb", json={"event": "present", "uid": "ZZ:99"})
    await hass.async_block_till_done()
    r = (await call(ws, "jukeb/status"))["result"]
    assert r["online"] is True and r["rssi"] == -50 and r["version"] == "2.0.0"
    assert r["last_scanned"]["uid"] == "ZZ:99" and r["last_scanned"]["known"] is False


async def test_tags_crud(hass, ws):
    reply = await call(ws, "jukeb/tags/set", uid="aa:01", name="Dino", media={"type": "file", "path": "dino.mp3"})
    assert reply["success"]
    assert reply["result"]["uid"] == "AA:01"
    assert reply["result"]["tag"]["name"] == "Dino"
    assert reply["result"]["tag"]["media"] == {"type": "file", "path": "dino.mp3"}

    reply = await call(ws, "jukeb/tags/list")
    assert list(reply["result"]["tags"]) == ["AA:01"]

    reply = await call(ws, "jukeb/tags/set", uid="AA:01", name="", media={"type": "file", "path": "dino.mp3"})
    assert not reply["success"]
    assert reply["error"] == {"code": "invalid", "message": "name is required"}

    reply = await call(ws, "jukeb/tags/set", uid="BB:02", name="x", media={"type": "file", "path": "missing.mp3"})
    assert reply["error"]["message"] == "file not found"

    reply = await call(ws, "jukeb/tags/delete", uid="AA:01")
    assert reply["success"]
    reply = await call(ws, "jukeb/tags/delete", uid="AA:01")
    assert reply["error"]["message"] == "unknown tag"
    assert (await call(ws, "jukeb/tags/list"))["result"]["tags"] == {}


async def test_play(hass, ws):
    play = async_mock_service(hass, "media_player", "play_media")
    async_mock_service(hass, "media_player", "volume_set")
    await call(ws, "jukeb/tags/set", uid="AA:01", name="Dino", media={"type": "file", "path": "dino.mp3"})
    reply = await call(ws, "jukeb/play", uid="AA:01")
    assert reply["success"] and reply["result"] == {}
    assert len(play) == 1
    reply = await call(ws, "jukeb/play", uid="ZZ:99")
    assert reply["error"]["message"] == "unknown tag"


async def test_removed_server_side_file_playback_commands(hass, ws):
    for msg_type in ("jukeb/media/play", "jukeb/pause"):
        reply = await call(ws, msg_type, path="dino.mp3")
        assert reply["error"]["code"] == "unknown_command"


async def test_media_resolves_to_playable_url(hass, ws, hass_client):
    """The uri in media/list resolves through HA's own websocket to a URL the browser can play."""
    files = (await call(ws, "jukeb/media/list"))["result"]["files"]
    uri = next(f["uri"] for f in files if f["path"] == "dino.mp3")
    reply = await call(ws, "media_source/resolve_media", media_content_id=uri)
    assert reply["success"] and reply["result"]["mime_type"] == "audio/mpeg"
    client = await hass_client()
    resp = await client.get(reply["result"]["url"])
    assert resp.status == 200 and await resp.read() == b"abc"


async def test_status_reports_enabled_and_active(hass, ws):
    r = (await call(ws, "jukeb/status"))["result"]
    assert r["enabled"] is True and r["active"] is True and r["active_reason"] == "active"
    assert "player_state" not in r
    reply = await call(ws, "jukeb/settings/set", settings={"enabled": False})
    assert reply["success"] and reply["result"]["settings"]["enabled"] is False
    r = (await call(ws, "jukeb/status"))["result"]
    assert r["enabled"] is False and r["active"] is False and r["active_reason"] == "off"


async def test_settings(hass, ws):
    reply = await call(ws, "jukeb/settings/get")
    assert reply["result"]["settings"] == {
        "player": "media_player.kitchen",
        "volume": 0.3,
        "on_remove": "pause",
        "remove_grace_s": 2,
        "enabled": True,
        "active_from": None,
        "active_until": None,
    }
    reply = await call(ws, "jukeb/settings/set", settings={"volume": 0.8, "on_remove": "stop"})
    assert reply["success"]
    assert reply["result"]["settings"]["volume"] == 0.8
    assert reply["result"]["settings"]["on_remove"] == "stop"
    assert reply["result"]["settings"]["player"] == "media_player.kitchen"
    reply = await call(ws, "jukeb/settings/set", settings={"player": "switch.x"})
    assert reply["error"]["message"] == "player must be a media_player entity"


async def test_media_list_and_delete(hass, ws):
    reply = await call(ws, "jukeb/media/list")
    prefix = "media-source://media_source/media/jukeb/"
    assert reply["result"]["files"] == [
        {"path": "cat.mp3", "size": 2, "uri": prefix + "cat.mp3"},
        {"path": "dino.mp3", "size": 3, "uri": prefix + "dino.mp3"},
    ]
    reply = await call(ws, "jukeb/media/delete", path="cat.mp3")
    assert reply["success"]
    assert [f["path"] for f in (await call(ws, "jukeb/media/list"))["result"]["files"]] == ["dino.mp3"]
    reply = await call(ws, "jukeb/media/delete", path="../secret.mp3")
    assert reply["error"]["code"] == "invalid"


async def test_media_delete_refused_when_used(hass, ws):
    await call(ws, "jukeb/tags/set", uid="AA:01", name="Dino", media={"type": "file", "path": "dino.mp3"})
    reply = await call(ws, "jukeb/media/delete", path="dino.mp3")
    assert not reply["success"]
    assert reply["error"]["message"] == "file is used by tag Dino"
    assert hass.data[DOMAIN].media.exists("dino.mp3")


async def test_non_admin_rejected(hass, setup_jukeb, hass_ws_client, hass_read_only_access_token):
    ws = await hass_ws_client(hass, hass_read_only_access_token)
    for msg_type in ("jukeb/status", "jukeb/tags/list", "jukeb/settings/get", "jukeb/media/list"):
        reply = await call(ws, msg_type)
        assert not reply["success"] and reply["error"]["code"] == "unauthorized"
