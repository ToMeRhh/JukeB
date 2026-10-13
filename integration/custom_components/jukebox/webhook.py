"""Webhook the box posts its events to."""

from __future__ import annotations

from http import HTTPStatus

from aiohttp import web
from aiohttp.web import Request

from homeassistant.core import HomeAssistant
from homeassistant.util import dt as dt_util

from .const import DOMAIN, EVENT_ABSENT, EVENT_HEARTBEAT, EVENT_PRESENT, EVENTS
from .store import StoreError, normalize_uid


def _error(message: str) -> web.Response:
    return web.json_response({"ok": False, "error": message}, status=HTTPStatus.BAD_REQUEST)


async def handle_webhook(hass: HomeAssistant, webhook_id: str, request: Request) -> web.Response:
    data = hass.data.get(DOMAIN)
    if data is None:
        return web.json_response({"ok": False, "error": "not set up"}, status=HTTPStatus.SERVICE_UNAVAILABLE)
    try:
        payload = await request.json()
    except ValueError:
        return _error("invalid json")
    if not isinstance(payload, dict):
        return _error("invalid json")
    event = payload.get("event")
    if event not in EVENTS:
        return _error("unknown event")

    now = dt_util.utcnow()
    if event == EVENT_HEARTBEAT:
        data.touch(payload, now)
        return web.json_response({"ok": True})

    try:
        uid = normalize_uid(payload.get("uid"))
    except StoreError:
        return _error("uid required")
    data.touch(payload, now)
    if event == EVENT_PRESENT:
        known = await data.controller.async_present(uid)
        return web.json_response({"ok": True, "known": known})
    assert event == EVENT_ABSENT
    await data.controller.async_absent(uid)
    return web.json_response({"ok": True})
