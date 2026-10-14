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
from .media import MediaLibrary
from .store import JukeBStore, StoreError, Tag

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


class JukeBController:
    """Decides what to play from present/absent events."""

    def __init__(self, hass: HomeAssistant, store: JukeBStore, media: MediaLibrary) -> None:
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
        if not self.is_active():
            return True  # known, but JukeB is off or outside its hours: nothing plays
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

    # ----- enabled / active hours -----

    def _in_window(self, now_local: datetime) -> bool:
        s = self._store.settings
        if s.active_from is None or s.active_until is None:
            return True
        now = now_local.hour * 60 + now_local.minute
        start = int(s.active_from[:2]) * 60 + int(s.active_from[3:])
        end = int(s.active_until[:2]) * 60 + int(s.active_until[3:])
        if start < end:
            return start <= now < end
        return now >= start or now < end  # window crosses midnight

    def is_active(self, now_local: datetime | None = None) -> bool:
        s = self._store.settings
        return s.enabled and self._in_window(now_local or dt_util.now())

    def active_reason(self, now_local: datetime | None = None) -> str:
        s = self._store.settings
        if not s.enabled:
            return "off"
        if not self._in_window(now_local or dt_util.now()):
            return f"outside {s.active_from}\u2013{s.active_until}"
        return "active"

    # ----- panel -----

    async def async_play(self, uid: str) -> None:
        tag = self._store.get_tag(uid)
        if tag is None:
            raise StoreError("unknown tag")
        await self._async_play_tag(uid, tag)

    def snapshot(self) -> dict[str, Any]:
        now_local = dt_util.now()
        return {
            "current_uid": self.current_uid,
            "last_scanned": self.last_scanned,
            "now_playing": self.now_playing,
            "last_error": self.last_error,
            "enabled": self._store.settings.enabled,
            "active": self.is_active(now_local),
            "active_reason": self.active_reason(now_local),
        }

    async def async_shutdown(self) -> None:
        self._cancel_grace()

    # ----- playback -----

    async def _async_play_tag(self, uid: str, tag: Tag) -> None:
        path, name = tag.media_path, tag.name
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
            _LOGGER.warning("JukeB: %s", self.last_error)
            return False
        try:
            await self._hass.services.async_call(
                "media_player", service, {"entity_id": player, **data}, blocking=True
            )
        except (HomeAssistantError, vol.Invalid) as err:
            self.last_error = f"{service} on {player} failed: {err}"
            _LOGGER.warning("JukeB: %s", self.last_error)
            return False
        self.last_error = None
        return True
