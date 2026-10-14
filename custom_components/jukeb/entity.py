"""Base class for the JukeB entities."""

from __future__ import annotations

from homeassistant.core import callback
from homeassistant.helpers.device_registry import DeviceInfo
from homeassistant.helpers.dispatcher import async_dispatcher_connect
from homeassistant.helpers.entity import Entity

from .const import DOMAIN, MANUFACTURER, SIGNAL_UPDATE
from .data import JukeBData


class JukeBEntity(Entity):
    _attr_has_entity_name = True
    _attr_should_poll = False

    def __init__(self, data: JukeBData, key: str) -> None:
        self._data = data
        self._attr_unique_id = f"{data.entry.entry_id}_{key}"
        self._attr_device_info = DeviceInfo(
            identifiers={(DOMAIN, data.entry.entry_id)},
            name="JukeB",
            manufacturer=MANUFACTURER,
        )

    async def async_added_to_hass(self) -> None:
        self.async_on_remove(async_dispatcher_connect(self.hass, SIGNAL_UPDATE, self._handle_update))

    @callback
    def _handle_update(self) -> None:
        self.async_write_ha_state()
