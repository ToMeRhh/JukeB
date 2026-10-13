from datetime import timedelta

import pytest
from homeassistant.exceptions import HomeAssistantError
from homeassistant.util import dt as dt_util
from pytest_homeassistant_custom_component.common import async_fire_time_changed, async_mock_service

from custom_components.jukebox.controller import BoxStatus, JukeboxController
from custom_components.jukebox.media import MediaLibrary
from custom_components.jukebox.store import JukeboxStore, StoreError

PLAYER = "media_player.kitchen"
URI = "media-source://media_source/media/jukebox/dino.mp3"


@pytest.fixture
def services(hass):
    return {
        name: async_mock_service(hass, "media_player", name)
        for name in ("volume_set", "play_media", "media_pause", "media_stop")
    }


@pytest.fixture
async def ctl(hass, hass_media_dir):
    store = JukeboxStore(hass, PLAYER)
    await store.async_load()
    media = MediaLibrary(hass)
    await media.async_setup()
    await media.async_save("dino.mp3", b"x")
    await media.async_save("cat.mp3", b"y")
    await store.async_set_tag("AA:01", "Dino", {"type": "file", "path": "dino.mp3"}, media.exists)
    await store.async_set_tag("BB:02", "Cat", {"type": "file", "path": "cat.mp3"}, media.exists)
    hass.states.async_set(PLAYER, "idle")
    controller = JukeboxController(hass, store, media)
    yield controller
    await controller.async_shutdown()


async def advance(hass, seconds):
    async_fire_time_changed(hass, dt_util.utcnow() + timedelta(seconds=seconds))
    await hass.async_block_till_done()


async def test_known_tag_plays_with_volume(hass, ctl, services):
    assert await ctl.async_present("AA:01") is True
    await hass.async_block_till_done()
    assert services["volume_set"][0].data == {"entity_id": PLAYER, "volume_level": 0.3}
    assert services["play_media"][0].data == {
        "entity_id": PLAYER,
        "media_content_id": URI,
        "media_content_type": "music",
    }
    assert ctl.current_uid == "AA:01"
    assert ctl.now_playing["uid"] == "AA:01" and ctl.now_playing["name"] == "Dino"
    assert ctl.now_playing["path"] == "dino.mp3"
    assert ctl.last_scanned["uid"] == "AA:01" and ctl.last_scanned["known"] is True
    assert ctl.last_error is None


async def test_disabled_volume_skips_volume_set(hass, ctl, services):
    await ctl._store.async_set_settings({"volume": None})
    assert await ctl.async_present("AA:01") is True
    await hass.async_block_till_done()
    assert services["volume_set"] == []
    assert len(services["play_media"]) == 1
    assert ctl.now_playing["uid"] == "AA:01"


async def test_unknown_tag_only_records_last_scanned(hass, ctl, services):
    assert await ctl.async_present("ZZ:99") is False
    await hass.async_block_till_done()
    assert services["play_media"] == []
    assert ctl.current_uid is None
    assert ctl.last_scanned == {"uid": "ZZ:99", "known": False, "at": ctl.last_scanned["at"]}


async def test_absent_after_grace_pauses(hass, ctl, services):
    await ctl.async_present("AA:01")
    await ctl.async_absent("AA:01")
    await hass.async_block_till_done()
    assert services["media_pause"] == []
    assert ctl.current_uid == "AA:01"  # still counted as present during grace
    await advance(hass, 3)
    assert len(services["media_pause"]) == 1
    assert ctl.current_uid is None and ctl.now_playing is None


async def test_same_tag_back_within_grace_keeps_playing(hass, ctl, services):
    await ctl.async_present("AA:01")
    await ctl.async_absent("AA:01")
    await advance(hass, 1)
    assert await ctl.async_present("AA:01") is True
    await advance(hass, 5)
    assert services["media_pause"] == []
    assert len(services["play_media"]) == 1
    assert ctl.current_uid == "AA:01"


async def test_other_tag_during_grace_plays_new_and_cancels_pause(hass, ctl, services):
    await ctl.async_present("AA:01")
    await ctl.async_absent("AA:01")
    await ctl.async_present("BB:02")
    await advance(hass, 5)
    assert services["media_pause"] == []
    assert [c.data["media_content_id"] for c in services["play_media"]] == [
        URI,
        "media-source://media_source/media/jukebox/cat.mp3",
    ]
    assert ctl.current_uid == "BB:02"


async def test_unknown_tag_during_grace_does_not_cancel(hass, ctl, services):
    await ctl.async_present("AA:01")
    await ctl.async_absent("AA:01")
    assert await ctl.async_present("ZZ:99") is False
    await ctl.async_absent("ZZ:99")
    await advance(hass, 5)
    assert len(services["media_pause"]) == 1
    assert ctl.current_uid is None


async def test_absent_for_other_uid_ignored(hass, ctl, services):
    await ctl.async_present("AA:01")
    await ctl.async_absent("BB:02")
    await advance(hass, 5)
    assert services["media_pause"] == []
    assert ctl.current_uid == "AA:01"


async def test_grace_zero_applies_immediately(hass, ctl, services):
    await ctl._store.async_set_settings({"remove_grace_s": 0, "on_remove": "stop"})
    await ctl.async_present("AA:01")
    await ctl.async_absent("AA:01")
    await hass.async_block_till_done()
    assert len(services["media_stop"]) == 1
    assert ctl.current_uid is None


async def test_on_remove_none_does_nothing(hass, ctl, services):
    await ctl._store.async_set_settings({"on_remove": "none"})
    await ctl.async_present("AA:01")
    await ctl.async_absent("AA:01")
    await advance(hass, 5)
    assert services["media_pause"] == [] and services["media_stop"] == []
    assert ctl.current_uid is None and ctl.now_playing is None


async def test_service_failure_records_last_error(hass, ctl, services):
    async def boom(call):
        raise HomeAssistantError("cast is asleep")

    hass.services.async_register("media_player", "play_media", boom)
    assert await ctl.async_present("AA:01") is True
    await hass.async_block_till_done()
    assert "cast is asleep" in ctl.last_error
    assert ctl.now_playing is None
    # a later success clears the error
    async_mock_service(hass, "media_player", "play_media")
    await ctl.async_play("BB:02")
    assert ctl.last_error is None


async def test_play_from_panel_does_not_change_current(hass, ctl, services):
    await ctl.async_play("BB:02")
    assert len(services["play_media"]) == 1
    assert ctl.current_uid is None
    assert ctl.now_playing["uid"] == "BB:02"
    with pytest.raises(StoreError, match="unknown tag"):
        await ctl.async_play("ZZ:99")


async def test_snapshot(hass, ctl, services):
    assert ctl.snapshot() == {
        "current_uid": None,
        "last_scanned": None,
        "now_playing": None,
        "last_error": None,
        "enabled": True,
        "active": True,
        "active_reason": "active",
    }


async def test_disabled_ignores_new_objects_but_records_scan(hass, ctl, services):
    await ctl._store.async_set_settings({"enabled": False})
    assert ctl.snapshot()["active"] is False and ctl.snapshot()["active_reason"] == "off"
    assert await ctl.async_present("AA:01") is True  # still "known" for the box's LED
    await hass.async_block_till_done()
    assert services["play_media"] == []
    assert ctl.current_uid is None
    assert ctl.last_scanned["uid"] == "AA:01" and ctl.last_scanned["known"] is True


async def test_disabling_keeps_current_song_and_removal_behaviour(hass, ctl, services):
    assert await ctl.async_present("AA:01") is True
    await hass.async_block_till_done()
    await ctl._store.async_set_settings({"enabled": False})
    assert ctl.now_playing["uid"] == "AA:01"
    await ctl.async_absent("AA:01")
    await advance(hass, 2)
    assert len(services["media_pause"]) == 1
    assert ctl.current_uid is None


@pytest.mark.parametrize(
    ("window", "local_hhmm", "active"),
    [
        (("07:00", "20:00"), "06:59", False),
        (("07:00", "20:00"), "07:00", True),
        (("07:00", "20:00"), "19:59", True),
        (("07:00", "20:00"), "20:00", False),
        (("19:00", "07:00"), "23:30", True),  # overnight
        (("19:00", "07:00"), "03:00", True),
        (("19:00", "07:00"), "12:00", False),
    ],
)
async def test_active_hours(hass, ctl, services, window, local_hhmm, active):
    await ctl._store.async_set_settings({"active_from": window[0], "active_until": window[1]})
    hh, mm = (int(x) for x in local_hhmm.split(":"))
    now_local = dt_util.now().replace(hour=hh, minute=mm, second=0)
    assert ctl.is_active(now_local) is active
    assert ctl.active_reason(now_local) == ("active" if active else f"outside {window[0]}\u2013{window[1]}")


async def test_outside_hours_ignores_new_objects(hass, ctl, services, freezer):
    await ctl._store.async_set_settings({"active_from": "07:00", "active_until": "20:00"})
    freezer.move_to(dt_util.now().replace(hour=22, minute=0))
    assert ctl.snapshot()["active"] is False
    assert await ctl.async_present("AA:01") is True
    await hass.async_block_till_done()
    assert services["play_media"] == []
    freezer.move_to(dt_util.now().replace(hour=9, minute=0))
    assert ctl.snapshot()["active"] is True
    assert await ctl.async_present("AA:01") is True
    await hass.async_block_till_done()
    assert len(services["play_media"]) == 1


def test_box_status_online_offline():
    box = BoxStatus()
    now = dt_util.utcnow()
    assert box.online(now) is False
    assert box.as_dict(now)["online"] is False and box.as_dict(now)["last_seen"] is None
    box.touch({"box_id": "jukebox", "version": "2.0.0", "rssi": -61, "uptime_s": 12}, now)
    assert box.online(now + timedelta(seconds=89)) is True
    assert box.online(now + timedelta(seconds=90)) is False
    d = box.as_dict(now)
    assert d["rssi"] == -61 and d["uptime_s"] == 12 and d["version"] == "2.0.0"
    assert d["box_id"] == "jukebox" and d["last_seen"] == now.isoformat()
    box.touch({"rssi": "weak"}, now)  # bad types are ignored, last_seen still updates
    assert box.as_dict(now)["rssi"] is None


async def test_missing_player_records_last_error(hass, ctl, services):
    # No media_player.kitchen state exists in this test: HA's entity service helper
    # silently skips missing entities, so the controller must check itself.
    hass.states.async_remove(PLAYER)
    assert await ctl.async_present("AA:01") is True
    await hass.async_block_till_done()
    assert services["play_media"] == []
    assert ctl.now_playing is None
    assert ctl.last_error == f"{PLAYER} is not available"


async def test_unavailable_player_records_last_error(hass, ctl, services):
    hass.states.async_set(PLAYER, "unavailable")
    await ctl.async_play("AA:01")
    assert services["play_media"] == []
    assert ctl.last_error == f"{PLAYER} is not available"
    hass.states.async_set(PLAYER, "idle")
    await ctl.async_play("AA:01")
    assert len(services["play_media"]) == 1
    assert ctl.last_error is None


async def test_duplicate_present_without_grace_replays(hass, ctl, services):
    # A lost `absent` (box could not deliver it) must not make the object dead:
    # a second `present` with no grace timer running plays again.
    await ctl.async_present("AA:01")
    await hass.async_block_till_done()
    await ctl.async_present("AA:01")
    await hass.async_block_till_done()
    assert len(services["play_media"]) == 2


async def test_grace_zero_does_not_block_on_the_service(hass, ctl, services):
    import asyncio

    release = asyncio.Event()

    async def slow_pause(call):
        await release.wait()

    hass.services.async_register("media_player", "media_pause", slow_pause)
    await ctl._store.async_set_settings({"remove_grace_s": 0})
    await ctl.async_present("AA:01")
    await hass.async_block_till_done()
    await asyncio.wait_for(ctl.async_absent("AA:01"), timeout=1)  # must return before the pause completes
    assert ctl.current_uid is None
    release.set()
    await hass.async_block_till_done()
