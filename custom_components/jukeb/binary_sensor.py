"""Online state of the box."""

from __future__ import annotations

from typing import Any

from homeassistant.components.binary_sensor import BinarySensorDeviceClass, BinarySensorEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback
from homeassistant.util import dt as dt_util

from .data import get_data
from .entity import JukeBEntity


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback) -> None:
    async_add_entities([JukeBOnlineSensor(get_data(hass))])


class JukeBOnlineSensor(JukeBEntity, BinarySensorEntity):
    _attr_device_class = BinarySensorDeviceClass.CONNECTIVITY
    _attr_translation_key = "online"

    def __init__(self, data) -> None:
        super().__init__(data, "online")

    @property
    def is_on(self) -> bool:
        return self._data.box.online(dt_util.utcnow())

    @property
    def extra_state_attributes(self) -> dict[str, Any]:
        box = self._data.box
        return {
            "box_id": box.box_id,
            "firmware": box.version,
            "rssi": box.rssi,
            "uptime_s": box.uptime_s,
            "last_seen": box.last_seen.isoformat() if box.last_seen else None,
        }
