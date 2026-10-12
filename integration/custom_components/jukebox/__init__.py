"""Jukebox: RFID music box integration."""

from __future__ import annotations

from homeassistant.components import webhook as ha_webhook
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant

from .const import CONF_PLAYER, CONF_WEBHOOK_ID, DATA_SETUP_DONE, DOMAIN
from .controller import BoxStatus, JukeboxController
from .data import JukeboxData
from .media import MediaLibrary
from .store import JukeboxStore
from .webhook import handle_webhook


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Set up Jukebox from a config entry."""
    store = JukeboxStore(hass, entry.data[CONF_PLAYER])
    await store.async_load()
    media = MediaLibrary(hass)
    await media.async_setup()
    controller = JukeboxController(hass, store, media)
    hass.data[DOMAIN] = JukeboxData(entry, store, media, controller, BoxStatus())

    ha_webhook.async_register(
        hass,
        DOMAIN,
        "Jukebox",
        entry.data[CONF_WEBHOOK_ID],
        handle_webhook,
        local_only=True,
        allowed_methods=["POST"],
    )

    if not hass.data.get(DATA_SETUP_DONE):
        # Global registrations survive entry reloads; do them once per HA run.
        # [ws]
        # [upload]
        # [static]
        hass.data[DATA_SETUP_DONE] = True
    # [panel]
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Unload Jukebox."""
    ha_webhook.async_unregister(hass, entry.data[CONF_WEBHOOK_ID])
    # [panel-remove]
    data: JukeboxData | None = hass.data.pop(DOMAIN, None)
    if data is not None:
        await data.controller.async_shutdown()
    return True
