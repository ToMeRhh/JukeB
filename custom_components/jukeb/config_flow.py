"""Config flow: webhook id + speaker, single instance."""

from __future__ import annotations

import re
from typing import Any

import voluptuous as vol

from homeassistant import config_entries
from homeassistant.config_entries import ConfigFlowResult
from homeassistant.helpers import selector

from .const import CONF_PLAYER, CONF_WEBHOOK_ID, DEFAULT_WEBHOOK_ID, DOMAIN, WEBHOOK_ID_PATTERN


def _schema(defaults: dict[str, Any]) -> vol.Schema:
    return vol.Schema(
        {
            vol.Required(
                CONF_WEBHOOK_ID, default=defaults.get(CONF_WEBHOOK_ID, DEFAULT_WEBHOOK_ID)
            ): str,
            vol.Required(CONF_PLAYER, default=defaults.get(CONF_PLAYER)): selector.EntitySelector(
                selector.EntitySelectorConfig(domain="media_player")
            ),
        }
    )


class JukeBConfigFlow(config_entries.ConfigFlow, domain=DOMAIN):
    """Handle the one-step user flow."""

    VERSION = 1

    async def async_step_user(self, user_input: dict[str, Any] | None = None) -> ConfigFlowResult:
        errors: dict[str, str] = {}
        if user_input is not None:
            webhook_id = user_input[CONF_WEBHOOK_ID].strip()
            if not re.fullmatch(WEBHOOK_ID_PATTERN, webhook_id):
                errors[CONF_WEBHOOK_ID] = "invalid_webhook_id"
            else:
                return self.async_create_entry(
                    title="JukeB",
                    data={CONF_WEBHOOK_ID: webhook_id, CONF_PLAYER: user_input[CONF_PLAYER]},
                )
        return self.async_show_form(
            step_id="user", data_schema=_schema(user_input or {}), errors=errors
        )
