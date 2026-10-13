"""Presence state machine, playback, and box online status."""

from __future__ import annotations

import logging
from dataclasses import dataclass
from datetime import datetime
from typing import Any

import voluptuous as vol

from homeassistant.const import STATE_UNAVAILABLE
from homeassistant.core import CALLBACK_TYPE, HomeAssistant, callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers.event import async_call_later
from homeassistant.util import dt as dt_util

from .const import MEDIA_CONTENT_TYPE, OFFLINE_AFTER_S, ON_REMOVE_NONE, ON_REMOVE_PAUSE
from .media import MediaError, MediaLibrary
from .store import JukeboxStore, StoreError, Tag

_LOGGER = logging.getLogger(__name__)


@dataclass
class BoxStatus:
    """What we know about the physical box, from its last event."""

    last_seen: datetime | None = None
    box_id: str | None = None
    version: str | None = None
    rssi: int | None = None
    uptime_s: int | None = None

    def touch(self, payload: dict[str, Any], now: datetime) -> None:
        self.last_seen = now
        if isinstance(payload.get("box_id"), str):
            self.box_id = payload["box_id"]
        if isinstance(payload.get("version"), str):
            self.version = payload["version"]
        if "rssi" in payload:
            self.rssi = payload["rssi"] if isinstance(payload["rssi"], int) else None
        if "uptime_s" in payload:
            self.uptime_s = payload["uptime_s"] if isinstance(payload["uptime_s"], int) else None

    def online(self, now: datetime) -> bool:
        return self.last_seen is not None and (now - self.last_seen).total_seconds() < OFFLINE_AFTER_S

    def as_dict(self, now: datetime) -> dict[str, Any]:
        return {
            "online": self.online(now),
            "last_seen": self.last_seen.isoformat() if self.last_seen else None,
            "box_id": self.box_id,
            "version": self.version,
            "rssi": self.rssi,
            "uptime_s": self.uptime_s,
        }


class JukeboxController:
    """Decides what to play from present/absent events."""

    def __init__(self, hass: HomeAssistant, store: JukeboxStore, media: MediaLibrary) -> None:
        self._hass = hass
        self._store = store
        self._media = media
        self.current_uid: str | None = None
        self.last_scanned: dict[str, Any] | None = None
        self.now_playing: dict[str, Any] | None = None
        self.last_error: str | None = None
        self._grace_cancel: CALLBACK_TYPE | None = None

    # ----- events from the box -----

    async def async_present(self, uid: str) -> bool:
        tag = self._store.get_tag(uid)
        self.last_scanned = {"uid": uid, "known": tag is not None, "at": dt_util.utcnow().isoformat()}
        if tag is None:
            return False
        if self.current_uid == uid and self._grace_cancel is not None:
            self._cancel_grace()
            return True  # back within the grace window: keep playing
        self._cancel_grace()
        self.current_uid = uid
        self._hass.async_create_task(self._async_play_tag(uid, tag))
        return True

    async def async_absent(self, uid: str) -> None:
        if uid != self.current_uid:
            return
        self._cancel_grace()
        grace = self._store.settings.remove_grace_s
        if grace <= 0:
            self._hass.async_create_task(self._async_removed())
            return
        self._grace_cancel = async_call_later(self._hass, grace, self._grace_expired)

    @callback
    def _grace_expired(self, _now: datetime) -> None:
        self._grace_cancel = None
        self._hass.async_create_task(self._async_removed())

    def _cancel_grace(self) -> None:
        if self._grace_cancel is not None:
            self._grace_cancel()
            self._grace_cancel = None

    async def _async_removed(self) -> None:
        self.current_uid = None
        self.now_playing = None
        action = self._store.settings.on_remove
        if action == ON_REMOVE_NONE:
            return
        service = "media_pause" if action == ON_REMOVE_PAUSE else "media_stop"
        await self._async_call(service, {})

    # ----- panel -----

    async def async_play(self, uid: str) -> None:
        tag = self._store.get_tag(uid)
        if tag is None:
            raise StoreError("unknown tag")
        await self._async_play_tag(uid, tag)

    async def async_play_file(self, path: str) -> None:
        """Play a library file on the speaker without touching the object state."""
        if not self._media.exists(path):
            raise MediaError("file not found")
        await self._async_play_path(path, name=path, uid=None)

    async def async_pause(self) -> None:
        await self._async_call("media_pause", {})

    def snapshot(self) -> dict[str, Any]:
        state = self._hass.states.get(self._store.settings.player)
        return {
            "current_uid": self.current_uid,
            "last_scanned": self.last_scanned,
            "now_playing": self.now_playing,
            "last_error": self.last_error,
            "player_state": state.state if state is not None else None,
        }

    async def async_shutdown(self) -> None:
        self._cancel_grace()

    # ----- playback -----

    async def _async_play_tag(self, uid: str, tag: Tag) -> None:
        await self._async_play_path(tag.media_path, name=tag.name, uid=uid)

    async def _async_play_path(self, path: str, *, name: str, uid: str | None) -> None:
        settings = self._store.settings
        ok = True
        if settings.volume is not None:
            ok = await self._async_call("volume_set", {"volume_level": settings.volume})
        if ok:
            ok = await self._async_call(
                "play_media",
                {"media_content_id": self._media.uri(path), "media_content_type": MEDIA_CONTENT_TYPE},
            )
        if ok:
            self.now_playing = {"uid": uid, "name": name, "path": path, "at": dt_util.utcnow().isoformat()}

    async def _async_call(self, service: str, data: dict[str, Any]) -> bool:
        player = self._store.settings.player
        state = self._hass.states.get(player)
        if state is None or state.state == STATE_UNAVAILABLE:
            # The entity service helper silently skips missing/unavailable entities.
            self.last_error = f"{player} is not available"
            _LOGGER.warning("Jukebox: %s", self.last_error)
            return False
        try:
            await self._hass.services.async_call(
                "media_player", service, {"entity_id": player, **data}, blocking=True
            )
        except (HomeAssistantError, vol.Invalid) as err:
            self.last_error = f"{service} on {player} failed: {err}"
            _LOGGER.warning("Jukebox: %s", self.last_error)
            return False
        self.last_error = None
        return True
