import pytest

from custom_components.jukebox.store import JukeboxStore, StoreError, normalize_uid

PLAYER = "media_player.kitchen"


async def make_store(hass):
    store = JukeboxStore(hass, PLAYER)
    await store.async_load()
    return store


def test_normalize_uid():
    assert normalize_uid(" 04:a3:7f:12 ") == "04:A3:7F:12"
    with pytest.raises(StoreError):
        normalize_uid("   ")


async def test_defaults_seed_player_from_entry(hass):
    store = await make_store(hass)
    s = store.settings
    assert s.player == PLAYER
    assert s.volume == 0.3
    assert s.on_remove == "pause"
    assert s.remove_grace_s == 2
    assert store.tags == {}


async def test_settings_roundtrip_and_clamping(hass, hass_storage):
    store = await make_store(hass)
    s = await store.async_set_settings(
        {"player": "media_player.den", "volume": 1.7, "on_remove": "stop", "remove_grace_s": 999}
    )
    assert s.player == "media_player.den"
    assert s.volume == 1.0
    assert s.on_remove == "stop"
    assert s.remove_grace_s == 60
    assert hass_storage["jukebox"]["data"]["settings"]["player"] == "media_player.den"

    reloaded = await make_store(hass)
    assert reloaded.settings == s


async def test_settings_validation(hass):
    store = await make_store(hass)
    with pytest.raises(StoreError, match="media_player"):
        await store.async_set_settings({"player": "light.kitchen"})
    with pytest.raises(StoreError, match="media_player"):
        await store.async_set_settings({"player": ""})
    with pytest.raises(StoreError, match="on_remove"):
        await store.async_set_settings({"on_remove": "explode"})
    with pytest.raises(StoreError, match="volume"):
        await store.async_set_settings({"volume": "loud"})


async def test_partial_settings_keep_others(hass):
    store = await make_store(hass)
    await store.async_set_settings({"volume": 0.5})
    assert store.settings.player == PLAYER
    assert store.settings.volume == 0.5


async def test_tag_upsert_delete(hass, hass_storage):
    store = await make_store(hass)
    uid, tag = await store.async_set_tag(
        "04:a3:7f:12", " Blue dinosaur ", {"type": "file", "path": "dino.mp3"}, lambda p: True
    )
    assert uid == "04:A3:7F:12"
    assert tag.name == "Blue dinosaur"
    assert tag.media_path == "dino.mp3"
    assert tag.added_at.endswith("+00:00")
    assert store.get_tag("04:A3:7F:12") == tag
    assert hass_storage["jukebox"]["data"]["tags"]["04:A3:7F:12"]["media"] == {
        "type": "file",
        "path": "dino.mp3",
    }

    _, updated = await store.async_set_tag(
        "04:A3:7F:12", "Dino", {"type": "file", "path": "dino2.mp3"}, lambda p: True
    )
    assert updated.added_at == tag.added_at  # preserved on update
    assert updated.name == "Dino"

    await store.async_delete_tag("04:A3:7F:12")
    assert store.get_tag("04:A3:7F:12") is None
    with pytest.raises(StoreError, match="unknown tag"):
        await store.async_delete_tag("04:A3:7F:12")


async def test_tag_validation(hass):
    store = await make_store(hass)
    with pytest.raises(StoreError, match="name is required"):
        await store.async_set_tag("AA", "  ", {"type": "file", "path": "a.mp3"}, lambda p: True)
    with pytest.raises(StoreError, match="unsupported media type"):
        await store.async_set_tag("AA", "x", {"type": "uri", "path": "a"}, lambda p: True)
    with pytest.raises(StoreError, match="file not found"):
        await store.async_set_tag("AA", "x", {"type": "file", "path": "a.mp3"}, lambda p: False)
    with pytest.raises(StoreError, match="uid is required"):
        await store.async_set_tag("", "x", {"type": "file", "path": "a.mp3"}, lambda p: True)


async def test_tag_limit(hass):
    store = await make_store(hass)
    store._data["tags"] = {f"{i:04X}": {"name": "n", "media": {"type": "file", "path": "a.mp3"}, "added_at": "x"} for i in range(500)}
    with pytest.raises(StoreError, match="tag limit"):
        await store.async_set_tag("FFFF:01", "x", {"type": "file", "path": "a.mp3"}, lambda p: True)
    # updating an existing tag is still allowed at the limit
    await store.async_set_tag("0001", "renamed", {"type": "file", "path": "a.mp3"}, lambda p: True)


async def test_tags_using(hass):
    store = await make_store(hass)
    await store.async_set_tag("01", "One", {"type": "file", "path": "a.mp3"}, lambda p: True)
    await store.async_set_tag("02", "Two", {"type": "file", "path": "b.mp3"}, lambda p: True)
    assert [u for u, _ in store.tags_using("a.mp3")] == ["01"]
    assert store.tags_using("zzz.mp3") == []


async def test_corrupt_storage_falls_back_to_defaults(hass, hass_storage):
    hass_storage["jukebox"] = {"version": 1, "key": "jukebox", "data": {"settings": "nope", "tags": []}}
    store = await make_store(hass)
    assert store.settings.player == PLAYER
    assert store.tags == {}
