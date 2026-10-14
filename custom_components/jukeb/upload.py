"""MP3 upload endpoint used by the panel."""

from __future__ import annotations

from http import HTTPStatus

from aiohttp import web

from homeassistant.components.http import KEY_HASS, HomeAssistantView
from homeassistant.exceptions import Unauthorized

from .const import MAX_UPLOAD_BYTES, UPLOAD_URL
from .data import get_data
from .media import MediaError


class JukeBUploadView(HomeAssistantView):
    url = UPLOAD_URL
    name = "api:jukeb:upload"

    async def post(self, request: web.Request) -> web.Response:
        if not request["hass_user"].is_admin:
            raise Unauthorized()
        hass = request.app[KEY_HASS]
        data = get_data(hass)
        request._client_max_size = MAX_UPLOAD_BYTES + 1024 * 1024  # noqa: SLF001
        form = await request.post()
        field = form.get("file")
        if field is None or not hasattr(field, "file"):
            return self.json({"error": "file field is required"}, HTTPStatus.BAD_REQUEST)
        content = await hass.async_add_executor_job(field.file.read)
        try:
            info = await data.media.async_save(field.filename, content)
        except MediaError as err:
            return self.json({"error": str(err)}, HTTPStatus.BAD_REQUEST)
        return self.json(info)
