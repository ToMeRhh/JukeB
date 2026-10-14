"""JukeB: RFID tags as play buttons for Home Assistant."""

from __future__ import annotations

from homeassistant.components import webhook as ha_webhook
from homeassistant.config_entries import ConfigEntry
from homeassistant.core import HomeAssistant
from homeassistant.helpers import device_registry as dr

from .const import CONF_PLAYER, CONF_WEBHOOK_ID, DATA_SETUP_DONE, DOMAIN, MANUFACTURER, PLATFORMS
from .controller import BoxStatus, JukeBController
from .data import JukeBData
from .media import MediaLibrary
from .panel import async_register_panel, async_register_static, async_remove_panel
from .store import JukeBStore
from .upload import JukeBUploadView
from .webhook import handle_webhook
from .websocket import async_register_websocket


async def async_setup_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Set up JukeB from a config entry."""
    store = JukeBStore(hass, entry.data[CONF_PLAYER])
    await store.async_load()
    media = MediaLibrary(hass)
    await media.async_setup()
    controller = JukeBController(hass, store, media)
    device = dr.async_get(hass).async_get_or_create(
        config_entry_id=entry.entry_id,
        identifiers={(DOMAIN, entry.entry_id)},
        name="JukeB",
        manufacturer=MANUFACTURER,
    )
    data = JukeBData(entry, store, media, controller, BoxStatus(), hass, device.id)
    store.add_settings_listener(data.notify)
    hass.data[DOMAIN] = data

    ha_webhook.async_register(
        hass,
        DOMAIN,
        "JukeB",
        entry.data[CONF_WEBHOOK_ID],
        handle_webhook,
        local_only=True,
        allowed_methods=["POST"],
    )

    if not hass.data.get(DATA_SETUP_DONE):
        # Global registrations survive entry reloads; do them once per HA run.
        async_register_websocket(hass)
        hass.http.register_view(JukeBUploadView())
        await async_register_static(hass)
        hass.data[DATA_SETUP_DONE] = True
    await async_register_panel(hass)
    await hass.config_entries.async_forward_entry_setups(entry, PLATFORMS)
    return True


async def async_unload_entry(hass: HomeAssistant, entry: ConfigEntry) -> bool:
    """Unload JukeB."""
    if not await hass.config_entries.async_unload_platforms(entry, PLATFORMS):
        return False
    ha_webhook.async_unregister(hass, entry.data[CONF_WEBHOOK_ID])
    async_remove_panel(hass)
    data: JukeBData | None = hass.data.pop(DOMAIN, None)
    if data is not None:
        data.shutdown()
        await data.controller.async_shutdown()
    return True
