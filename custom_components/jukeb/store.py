"""Persistent settings and tag registry (.storage/jukeb)."""

from __future__ import annotations

import re
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any

from homeassistant.core import HomeAssistant
from homeassistant.helpers.storage import Store
from homeassistant.util import dt as dt_util

from .const import (
    DEFAULT_GRACE_S,
    DEFAULT_ON_REMOVE,
    DEFAULT_VOLUME,
    MAX_GRACE_S,
    MAX_TAGS,
    MEDIA_TYPE_FILE,
    ON_REMOVE_CHOICES,
    STORAGE_KEY,
    STORAGE_VERSION,
)


class StoreError(ValueError):
    """Validation error; the message is safe to show in the UI."""


def normalize_uid(uid: Any) -> str:
    text = "".join(str(uid or "").split()).upper()
    if not text:
        raise StoreError("uid is required")
    return text


_TIME_RE = re.compile(r"^(\d{1,2}):(\d{2})$")


def _parse_time(value: Any, key: str) -> str | None:
    """Return a normalised "HH:MM" string, or None for null/empty."""
    if value is None or value == "":
        return None
    m = _TIME_RE.match(str(value).strip())
    if not m or int(m.group(1)) > 23 or int(m.group(2)) > 59:
        raise StoreError(f"{key} must be a time like HH:MM")
    return f"{int(m.group(1)):02d}:{m.group(2)}"


@dataclass(frozen=True)
class Settings:
    player: str
    volume: float | None  # None: do not touch the speaker's volume before playing
    on_remove: str
    remove_grace_s: int
    enabled: bool = True
    active_from: str | None = None  # "HH:MM" local time; both set = window applies
    active_until: str | None = None

    def as_dict(self) -> dict[str, Any]:
        return {
            "player": self.player,
            "volume": self.volume,
            "on_remove": self.on_remove,
            "remove_grace_s": self.remove_grace_s,
            "enabled": self.enabled,
            "active_from": self.active_from,
            "active_until": self.active_until,
        }


@dataclass(frozen=True)
class Tag:
    name: str
    media_type: str
    media_path: str
    added_at: str

    def as_dict(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "media": {"type": self.media_type, "path": self.media_path},
            "added_at": self.added_at,
        }

    @classmethod
    def from_dict(cls, raw: Any) -> Tag | None:
        if not isinstance(raw, dict):
            return None
        media = raw.get("media")
        if not isinstance(media, dict):
            return None
        name, mtype, path = raw.get("name"), media.get("type"), media.get("path")
        if not all(isinstance(v, str) and v for v in (name, mtype, path)):
            return None
        return cls(name, mtype, path, str(raw.get("added_at", "")))


def _validate_settings(current: Settings, partial: dict[str, Any]) -> Settings:
    values = current.as_dict()
    for key, value in partial.items():
        if key not in values:
            raise StoreError(f"unknown setting {key}")
        values[key] = value

    player = values["player"]
    if not isinstance(player, str) or not player.startswith("media_player."):
        raise StoreError("player must be a media_player entity")

    volume: float | None = None
    if values["volume"] is not None:
        try:
            volume = float(values["volume"])
        except (TypeError, ValueError) as err:
            raise StoreError("volume must be a number between 0 and 1") from err
        volume = min(1.0, max(0.0, volume))

    on_remove = values["on_remove"]
    if on_remove not in ON_REMOVE_CHOICES:
        raise StoreError("on_remove must be none, pause or stop")

    try:
        grace = int(values["remove_grace_s"])
    except (TypeError, ValueError) as err:
        raise StoreError("remove_grace_s must be a whole number of seconds") from err
    grace = min(MAX_GRACE_S, max(0, grace))

    enabled = values["enabled"]
    if not isinstance(enabled, bool):
        raise StoreError("enabled must be true or false")

    active_from = _parse_time(values["active_from"], "active_from")
    active_until = _parse_time(values["active_until"], "active_until")
    if (active_from is None) != (active_until is None):
        raise StoreError("active_from and active_until must both be set or both be empty")
    if active_from is not None and active_from == active_until:
        raise StoreError("active_from and active_until must differ")

    return Settings(player, volume, on_remove, grace, enabled, active_from, active_until)


class JukeBStore:
    """Owns the storage document and validates every write."""

    def __init__(self, hass: HomeAssistant, default_player: str) -> None:
        self._store: Store[dict[str, Any]] = Store(hass, STORAGE_VERSION, STORAGE_KEY)
        self._default_player = default_player
        self._data: dict[str, Any] = {"settings": {}, "tags": {}}
        self._settings = Settings(default_player, DEFAULT_VOLUME, DEFAULT_ON_REMOVE, DEFAULT_GRACE_S)
        self._listeners: list[Callable[[], None]] = []

    def add_settings_listener(self, listener: Callable[[], None]) -> None:
        """Call listener (on the event loop) after every settings write."""
        self._listeners.append(listener)

    async def async_load(self) -> None:
        raw = await self._store.async_load()
        defaults = Settings(self._default_player, DEFAULT_VOLUME, DEFAULT_ON_REMOVE, DEFAULT_GRACE_S)
        settings_raw = raw.get("settings") if isinstance(raw, dict) else None
        tags_raw = raw.get("tags") if isinstance(raw, dict) else None
        if not isinstance(settings_raw, dict):
            settings_raw = {}
        try:
            self._settings = _validate_settings(defaults, settings_raw)
        except StoreError:
            self._settings = defaults
        tags: dict[str, Any] = {}
        if isinstance(tags_raw, dict):
            for uid, tag_raw in tags_raw.items():
                tag = Tag.from_dict(tag_raw)
                if tag is not None:
                    tags[str(uid)] = tag.as_dict()
        self._data = {"settings": self._settings.as_dict(), "tags": tags}

    async def _async_save(self) -> None:
        self._data["settings"] = self._settings.as_dict()
        await self._store.async_save(self._data)

    # ----- settings -----

    @property
    def settings(self) -> Settings:
        return self._settings

    async def async_set_settings(self, partial: dict[str, Any]) -> Settings:
        if not isinstance(partial, dict):
            raise StoreError("settings must be an object")
        self._settings = _validate_settings(self._settings, partial)
        await self._async_save()
        for listener in self._listeners:
            listener()
        return self._settings

    # ----- tags -----

    @property
    def tags(self) -> dict[str, Tag]:
        return {uid: Tag.from_dict(raw) for uid, raw in self._data["tags"].items()}

    def get_tag(self, uid: str) -> Tag | None:
        raw = self._data["tags"].get(uid)
        return Tag.from_dict(raw) if raw else None

    async def async_set_tag(
        self, uid: str, name: str, media: Any, file_exists: Callable[[str], bool]
    ) -> tuple[str, Tag]:
        uid = normalize_uid(uid)
        name = str(name or "").strip()
        if not name:
            raise StoreError("name is required")
        if not isinstance(media, dict) or media.get("type") != MEDIA_TYPE_FILE:
            raise StoreError("unsupported media type")
        path = str(media.get("path") or "")
        if not path or not file_exists(path):
            raise StoreError("file not found")
        existing = self.get_tag(uid)
        if existing is None and len(self._data["tags"]) >= MAX_TAGS:
            raise StoreError("tag limit reached")
        added_at = existing.added_at if existing else dt_util.utcnow().isoformat()
        tag = Tag(name, MEDIA_TYPE_FILE, path, added_at)
        self._data["tags"][uid] = tag.as_dict()
        await self._async_save()
        return uid, tag

    async def async_delete_tag(self, uid: str) -> None:
        uid = normalize_uid(uid)
        if uid not in self._data["tags"]:
            raise StoreError("unknown tag")
        del self._data["tags"][uid]
        await self._async_save()

    def tags_using(self, path: str) -> list[tuple[str, Tag]]:
        return [(uid, tag) for uid, tag in self.tags.items() if tag.media_path == path]
