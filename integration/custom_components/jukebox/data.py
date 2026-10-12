"""Runtime objects shared by the webhook, websocket and views."""

from __future__ import annotations

from dataclasses import dataclass

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.exceptions import HomeAssistantError

from .const import DOMAIN
from .controller import BoxStatus, JukeboxController
from .media import MediaLibrary
from .store import JukeboxStore


@dataclass
class JukeboxData:
    entry: ConfigEntry
    store: JukeboxStore
    media: MediaLibrary
    controller: JukeboxController
    box: BoxStatus


def get_data(hass: HomeAssistant) -> JukeboxData:
    data = hass.data.get(DOMAIN)
    if data is None:
        raise HomeAssistantError("Jukebox is not set up")
    return data
