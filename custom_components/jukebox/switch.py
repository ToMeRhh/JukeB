"""Enabled toggle, mirrored with the panel and the settings store."""

from __future__ import annotations

from typing import Any

from homeassistant.components.switch import SwitchEntity
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers.entity_platform import AddEntitiesCallback

from .data import get_data
from .entity import JukeboxEntity


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry, async_add_entities: AddEntitiesCallback) -> None:
    async_add_entities([JukeboxEnabledSwitch(get_data(hass))])


class JukeboxEnabledSwitch(JukeboxEntity, SwitchEntity):
    _attr_name = "Enabled"

    def __init__(self, data) -> None:
        super().__init__(data, "enabled")

    @property
    def is_on(self) -> bool:
        return self._data.store.settings.enabled

    async def async_turn_on(self, **kwargs: Any) -> None:
        await self._data.store.async_set_settings({"enabled": True})

    async def async_turn_off(self, **kwargs: Any) -> None:
        await self._data.store.async_set_settings({"enabled": False})
