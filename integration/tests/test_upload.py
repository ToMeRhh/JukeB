import aiohttp

from custom_components.jukebox.const import DOMAIN

URL = "/api/jukebox/upload"


def form(name, data=b"ID3abc"):
    # Browsers send the filename raw (RFC 7578); aiohttp would percent-encode it by default.
    f = aiohttp.FormData(quote_fields=False)
    f.add_field("file", data, filename=name, content_type="audio/mpeg")
    return f


async def test_upload_ok(hass, setup_jukebox, hass_client):
    client = await hass_client()
    resp = await client.post(URL, data=form("My Song.mp3"))
    assert resp.status == 200
    assert await resp.json() == {"path": "My Song.mp3", "size": 6}
    assert hass.data[DOMAIN].media.exists("My Song.mp3")


async def test_upload_rejects_non_mp3(hass, setup_jukebox, hass_client):
    client = await hass_client()
    resp = await client.post(URL, data=form("song.wav"))
    assert resp.status == 400
    assert await resp.json() == {"error": "only .mp3 files are supported"}


async def test_upload_requires_file_field(hass, setup_jukebox, hass_client):
    client = await hass_client()
    f = aiohttp.FormData()
    f.add_field("other", "x")
    resp = await client.post(URL, data=f)
    assert resp.status == 400
    assert await resp.json() == {"error": "file field is required"}


async def test_upload_requires_admin(hass, setup_jukebox, hass_client, hass_read_only_access_token):
    client = await hass_client(hass_read_only_access_token)
    resp = await client.post(URL, data=form("a.mp3"))
    assert resp.status == 401


async def test_upload_requires_auth(hass, setup_jukebox, hass_client_no_auth):
    client = await hass_client_no_auth()
    resp = await client.post(URL, data=form("a.mp3"))
    assert resp.status == 401
