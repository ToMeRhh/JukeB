"""MP3 files under <media dir>/jukebox/."""

from __future__ import annotations

import os
import re
import unicodedata
from pathlib import Path
from typing import Any

from homeassistant.core import HomeAssistant

from .const import MAX_UPLOAD_BYTES, MEDIA_SUBDIR

# Path separators, drive/stream separators and control characters are dropped; every
# other character (Hebrew, accents, punctuation) is kept so the name the user sees is
# the name on disk.
_FORBIDDEN = re.compile(r"[/\\:\x00-\x1f\x7f]")


class MediaError(ValueError):
    """Validation error; the message is safe to show in the UI."""


def sanitize_filename(name: Any) -> str:
    text = unicodedata.normalize("NFC", str(name or ""))
    text = _FORBIDDEN.sub("", text).strip()
    if not text.lower().endswith(".mp3"):
        raise MediaError("only .mp3 files are supported")
    stem = text[:-4].strip(". ")
    if not stem:
        raise MediaError("file name is empty")
    return f"{stem}.mp3"


class MediaLibrary:
    """List, store and delete MP3 files; build media-source URIs."""

    def __init__(self, hass: HomeAssistant) -> None:
        self._hass = hass
        if hass.config.media_dirs:
            self.dir_name, base = next(iter(hass.config.media_dirs.items()))
        else:
            self.dir_name, base = "local", hass.config.path("media")
        self.root = Path(base) / MEDIA_SUBDIR

    async def async_setup(self) -> None:
        await self._hass.async_add_executor_job(self.root.mkdir, 0o755, True, True)

    def _resolve(self, path: Any) -> Path:
        """Return the absolute file path, refusing anything outside the root."""
        text = str(path or "")
        if not text or "/" in text or "\\" in text or text in (".", ".."):
            raise MediaError("invalid file name")
        full = (self.root / text).resolve()
        if full.parent != self.root.resolve():
            raise MediaError("invalid file name")
        return full

    def exists(self, path: Any) -> bool:
        try:
            return self._resolve(path).is_file()
        except MediaError:
            return False

    def uri(self, path: str) -> str:
        return f"media-source://media_source/{self.dir_name}/{MEDIA_SUBDIR}/{path}"

    def _list(self) -> list[dict[str, Any]]:
        files = []
        for entry in sorted(self.root.iterdir(), key=lambda p: p.name.lower()):
            if entry.is_file() and entry.suffix.lower() == ".mp3":
                files.append({"path": entry.name, "size": entry.stat().st_size})
        return files

    async def async_list(self) -> list[dict[str, Any]]:
        return await self._hass.async_add_executor_job(self._list)

    def _save(self, name: str, data: bytes) -> dict[str, Any]:
        target = self._resolve(name)
        tmp = target.with_name(target.name + ".tmp")
        with open(tmp, "wb") as handle:
            handle.write(data)
        os.replace(tmp, target)
        return {"path": name, "size": len(data)}

    async def async_save(self, filename: Any, data: bytes) -> dict[str, Any]:
        name = sanitize_filename(filename)
        if len(data) > MAX_UPLOAD_BYTES:
            raise MediaError("file is too large (max 50 MB)")
        return await self._hass.async_add_executor_job(self._save, name, data)

    def _delete(self, path: str) -> None:
        full = self._resolve(path)
        if not full.is_file():
            raise MediaError("file not found")
        full.unlink()

    async def async_delete(self, path: Any) -> None:
        await self._hass.async_add_executor_job(self._delete, path)
