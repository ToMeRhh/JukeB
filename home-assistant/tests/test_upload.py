import aiohttp

from custom_components.jukeb.const import DOMAIN

URL = "/api/jukeb/upload"


def form(name, data=b"ID3abc"):
    # Browsers send the filename raw (RFC 7578); aiohttp would percent-encode it by default.
    f = aiohttp.FormData(quote_fields=False)
    f.add_field("file", data, filename=name, content_type="audio/mpeg")
    return f


async def test_upload_ok(hass, setup_jukeb, hass_client):
    client = await hass_client()
    resp = await client.post(URL, data=form("My Song.mp3"))
    assert resp.status == 200
    assert await resp.json() == {"path": "My Song.mp3", "size": 6}
    assert hass.data[DOMAIN].media.exists("My Song.mp3")


async def test_upload_unicode_name_is_kept(hass, setup_jukeb, hass_client, hass_media_dir):
    client = await hass_client()
    resp = await client.post(URL, data=form("שיר - ילדים.mp3"))
    assert resp.status == 200
    assert await resp.json() == {"path": "שיר - ילדים.mp3", "size": 6}
    assert (hass_media_dir / "jukeb" / "שיר - ילדים.mp3").read_bytes() == b"ID3abc"


async def test_unicode_file_is_served_by_media_source(hass, setup_jukeb, hass_client, hass_media_dir):
    """The URI handed to play_media must resolve to a URL HA actually serves (what Cast fetches)."""
    from homeassistant.components import media_source

    client = await hass_client()
    await client.post(URL, data=form("שיר.mp3", b"ID3hebrew"))
    uri = hass.data[DOMAIN].media.uri("שיר.mp3")
    play = await media_source.async_resolve_media(hass, uri, None)
    assert play.mime_type == "audio/mpeg"
    resp = await client.get(play.url)
    assert resp.status == 200
    assert await resp.read() == b"ID3hebrew"


async def test_upload_rejects_non_mp3(hass, setup_jukeb, hass_client):
    client = await hass_client()
    resp = await client.post(URL, data=form("song.wav"))
    assert resp.status == 400
    assert await resp.json() == {"error": "only .mp3 files are supported"}


async def test_upload_requires_file_field(hass, setup_jukeb, hass_client):
    client = await hass_client()
    f = aiohttp.FormData()
    f.add_field("other", "x")
    resp = await client.post(URL, data=f)
    assert resp.status == 400
    assert await resp.json() == {"error": "file field is required"}


async def test_upload_requires_admin(hass, setup_jukeb, hass_client, hass_read_only_access_token):
    client = await hass_client(hass_read_only_access_token)
    resp = await client.post(URL, data=form("a.mp3"))
    assert resp.status == 401


async def test_upload_requires_auth(hass, setup_jukeb, hass_client_no_auth):
    client = await hass_client_no_auth()
    resp = await client.post(URL, data=form("a.mp3"))
    assert resp.status == 401
