"""Websocket API used by the JukeB panel. All commands are admin only."""

from __future__ import annotations

from typing import Any

import voluptuous as vol

from homeassistant.components import websocket_api
from homeassistant.core import HomeAssistant, callback
from homeassistant.util import dt as dt_util

from .const import CONF_WEBHOOK_ID, VERSION
from .data import get_data
from .media import MediaError
from .store import StoreError, normalize_uid

ERR_INVALID = "invalid"


def _send_invalid(connection: websocket_api.ActiveConnection, msg_id: int, err: Exception) -> None:
    connection.send_error(msg_id, ERR_INVALID, str(err))


@websocket_api.require_admin
@websocket_api.websocket_command({vol.Required("type"): "jukeb/status"})
@callback
def ws_status(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict[str, Any]) -> None:
    data = get_data(hass)
    result = data.box.as_dict(dt_util.utcnow())
    result.update(data.controller.snapshot())
    result["webhook_id"] = data.entry.data[CONF_WEBHOOK_ID]
    result["integration_version"] = VERSION
    connection.send_result(msg["id"], result)


@websocket_api.require_admin
@websocket_api.websocket_command({vol.Required("type"): "jukeb/tags/list"})
@callback
def ws_tags_list(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict[str, Any]) -> None:
    tags = get_data(hass).store.tags
    connection.send_result(msg["id"], {"tags": {uid: tag.as_dict() for uid, tag in tags.items()}})


@websocket_api.require_admin
@websocket_api.websocket_command(
    {
        vol.Required("type"): "jukeb/tags/set",
        vol.Required("uid"): str,
        vol.Required("name"): str,
        vol.Required("media"): dict,
    }
)
@websocket_api.async_response
async def ws_tags_set(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict[str, Any]) -> None:
    data = get_data(hass)
    try:
        uid, tag = await data.store.async_set_tag(msg["uid"], msg["name"], msg["media"], data.media.exists)
    except StoreError as err:
        _send_invalid(connection, msg["id"], err)
        return
    connection.send_result(msg["id"], {"uid": uid, "tag": tag.as_dict()})


@websocket_api.require_admin
@websocket_api.websocket_command({vol.Required("type"): "jukeb/tags/delete", vol.Required("uid"): str})
@websocket_api.async_response
async def ws_tags_delete(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict[str, Any]) -> None:
    try:
        await get_data(hass).store.async_delete_tag(msg["uid"])
    except StoreError as err:
        _send_invalid(connection, msg["id"], err)
        return
    connection.send_result(msg["id"], {})


@websocket_api.require_admin
@websocket_api.websocket_command({vol.Required("type"): "jukeb/play", vol.Required("uid"): str})
@websocket_api.async_response
async def ws_play(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict[str, Any]) -> None:
    data = get_data(hass)
    try:
        await data.controller.async_play(normalize_uid(msg["uid"]))
    except StoreError as err:
        _send_invalid(connection, msg["id"], err)
        return
    connection.send_result(msg["id"], {})


@websocket_api.require_admin
@websocket_api.websocket_command({vol.Required("type"): "jukeb/settings/get"})
@callback
def ws_settings_get(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict[str, Any]) -> None:
    connection.send_result(msg["id"], {"settings": get_data(hass).store.settings.as_dict()})


@websocket_api.require_admin
@websocket_api.websocket_command({vol.Required("type"): "jukeb/settings/set", vol.Required("settings"): dict})
@websocket_api.async_response
async def ws_settings_set(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict[str, Any]) -> None:
    try:
        settings = await get_data(hass).store.async_set_settings(msg["settings"])
    except StoreError as err:
        _send_invalid(connection, msg["id"], err)
        return
    connection.send_result(msg["id"], {"settings": settings.as_dict()})


@websocket_api.require_admin
@websocket_api.websocket_command({vol.Required("type"): "jukeb/media/list"})
@websocket_api.async_response
async def ws_media_list(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict[str, Any]) -> None:
    media = get_data(hass).media
    files = [{**f, "uri": media.uri(f["path"])} for f in await media.async_list()]
    connection.send_result(msg["id"], {"files": files})


@websocket_api.require_admin
@websocket_api.websocket_command({vol.Required("type"): "jukeb/media/delete", vol.Required("path"): str})
@websocket_api.async_response
async def ws_media_delete(hass: HomeAssistant, connection: websocket_api.ActiveConnection, msg: dict[str, Any]) -> None:
    data = get_data(hass)
    users = data.store.tags_using(msg["path"])
    if users:
        _send_invalid(connection, msg["id"], MediaError(f"file is used by tag {users[0][1].name}"))
        return
    try:
        await data.media.async_delete(msg["path"])
    except MediaError as err:
        _send_invalid(connection, msg["id"], err)
        return
    connection.send_result(msg["id"], {})


COMMANDS = (
    ws_status,
    ws_tags_list,
    ws_tags_set,
    ws_tags_delete,
    ws_play,
    ws_settings_get,
    ws_settings_set,
    ws_media_list,
    ws_media_delete,
)


@callback
def async_register_websocket(hass: HomeAssistant) -> None:
    for command in COMMANDS:
        websocket_api.async_register_command(hass, command)
