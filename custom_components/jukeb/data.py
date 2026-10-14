"""Runtime objects shared by the webhook, websocket, views and entities."""

from __future__ import annotations

from dataclasses import dataclass, field
from datetime import datetime
from typing import Any

from homeassistant.config_entries import ConfigEntry
from homeassistant.core import CALLBACK_TYPE, HomeAssistant, callback
from homeassistant.exceptions import HomeAssistantError
from homeassistant.helpers import device_registry as dr
from homeassistant.helpers.dispatcher import async_dispatcher_send
from homeassistant.helpers.event import async_call_later

from .const import DOMAIN, OFFLINE_AFTER_S, SIGNAL_UPDATE
from .controller import BoxStatus, JukeBController
from .media import MediaLibrary
from .store import JukeBStore


@dataclass
class JukeBData:
    entry: ConfigEntry
    store: JukeBStore
    media: MediaLibrary
    controller: JukeBController
    box: BoxStatus
    hass: HomeAssistant
    device_id: str
    _offline_cancel: CALLBACK_TYPE | None = field(default=None, repr=False)

    @callback
    def notify(self) -> None:
        """Tell entities (and anything else listening) that state may have changed."""
        async_dispatcher_send(self.hass, SIGNAL_UPDATE)

    @callback
    def touch(self, payload: dict[str, Any], now: datetime) -> None:
        """Record an event from the box: online status, firmware version, offline timer."""
        previous_version = self.box.version
        self.box.touch(payload, now)
        if self.box.version != previous_version:
            dr.async_get(self.hass).async_update_device(self.device_id, sw_version=self.box.version)
        if self._offline_cancel is not None:
            self._offline_cancel()
        self._offline_cancel = async_call_later(self.hass, OFFLINE_AFTER_S, self._offline_timeout)
        self.notify()

    @callback
    def _offline_timeout(self, _now: datetime) -> None:
        self._offline_cancel = None
        self.notify()

    @callback
    def shutdown(self) -> None:
        if self._offline_cancel is not None:
            self._offline_cancel()
            self._offline_cancel = None


def get_data(hass: HomeAssistant) -> JukeBData:
    data = hass.data.get(DOMAIN)
    if data is None:
        raise HomeAssistantError("JukeB is not set up")
    return data
