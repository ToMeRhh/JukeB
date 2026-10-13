import pytest

from custom_components.jukebox.media import MediaError, MediaLibrary, sanitize_filename


def test_sanitize_filename():
    assert sanitize_filename("My Song.mp3") == "My Song.mp3"
    assert sanitize_filename("  weird/..\\name!.mp3 ") == "weird..name!.mp3"
    assert sanitize_filename("a:b\x00c.mp3") == "abc.mp3"
    assert sanitize_filename("../../x.mp3") == "x.mp3"  # separators are stripped, never honoured
    for bad in ("", "song.wav", "..mp3", ".mp3"):
        with pytest.raises(MediaError):
            sanitize_filename(bad)


def test_sanitize_keeps_unicode_and_normalizes():
    assert sanitize_filename("שיר - ילדים.mp3") == "שיר - ילדים.mp3"
    assert sanitize_filename("Café.mp3") == "Café.mp3"
    # macOS sends decomposed (NFD) names; stored names are NFC so lookups match.
    assert sanitize_filename("Cafe\u0301.mp3") == "Café.mp3"


async def test_save_list_delete_unicode(hass, hass_media_dir):
    lib = MediaLibrary(hass)
    await lib.async_setup()
    assert await lib.async_save("שיר.mp3", b"ID3") == {"path": "שיר.mp3", "size": 3}
    assert (hass_media_dir / "jukebox" / "שיר.mp3").exists()
    assert lib.exists("שיר.mp3")
    assert await lib.async_list() == [{"path": "שיר.mp3", "size": 3}]
    assert lib.uri("שיר.mp3") == "media-source://media_source/media/jukebox/שיר.mp3"
    await lib.async_delete("שיר.mp3")
    assert await lib.async_list() == []


async def test_setup_creates_folder_and_uri(hass, hass_media_dir):
    lib = MediaLibrary(hass)
    await lib.async_setup()
    assert (hass_media_dir / "jukebox").is_dir()
    assert lib.dir_name == "media"
    assert lib.uri("dino.mp3") == "media-source://media_source/media/jukebox/dino.mp3"


async def test_save_list_delete(hass, hass_media_dir):
    lib = MediaLibrary(hass)
    await lib.async_setup()
    info = await lib.async_save("Dino.mp3", b"ID3xyz")
    assert info == {"path": "Dino.mp3", "size": 6}
    assert lib.exists("Dino.mp3")
    assert not lib.exists("nope.mp3")
    (hass_media_dir / "jukebox" / "notes.txt").write_text("ignored")
    assert await lib.async_list() == [{"path": "Dino.mp3", "size": 6}]
    await lib.async_delete("Dino.mp3")
    assert await lib.async_list() == []
    with pytest.raises(MediaError, match="file not found"):
        await lib.async_delete("Dino.mp3")


async def test_save_rejects_bad_names_and_size(hass, hass_media_dir):
    lib = MediaLibrary(hass)
    await lib.async_setup()
    with pytest.raises(MediaError):
        await lib.async_save("x.wav", b"abc")
    with pytest.raises(MediaError, match="too large"):
        await lib.async_save("x.mp3", b"a" * (50 * 1024 * 1024 + 1))


async def test_exists_and_delete_reject_traversal(hass, hass_media_dir):
    lib = MediaLibrary(hass)
    await lib.async_setup()
    (hass_media_dir / "secret.mp3").write_bytes(b"x")
    assert not lib.exists("../secret.mp3")
    with pytest.raises(MediaError):
        await lib.async_delete("../secret.mp3")
    assert (hass_media_dir / "secret.mp3").exists()
