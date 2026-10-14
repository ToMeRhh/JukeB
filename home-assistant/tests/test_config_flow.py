from homeassistant import config_entries
from homeassistant.data_entry_flow import FlowResultType

from custom_components.jukeb.const import CONF_PLAYER, CONF_WEBHOOK_ID, DOMAIN


async def test_user_flow_creates_entry(hass):
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_USER}
    )
    assert result["type"] == FlowResultType.FORM
    assert result["step_id"] == "user"

    result = await hass.config_entries.flow.async_configure(
        result["flow_id"],
        {CONF_WEBHOOK_ID: "jukeb", CONF_PLAYER: "media_player.kitchen"},
    )
    assert result["type"] == FlowResultType.CREATE_ENTRY
    assert result["title"] == "JukeB"
    assert result["data"] == {
        CONF_WEBHOOK_ID: "jukeb",
        CONF_PLAYER: "media_player.kitchen",
    }


async def test_invalid_webhook_id_shows_error(hass):
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_USER}
    )
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"],
        {CONF_WEBHOOK_ID: "Not Valid!", CONF_PLAYER: "media_player.kitchen"},
    )
    assert result["type"] == FlowResultType.FORM
    assert result["errors"] == {CONF_WEBHOOK_ID: "invalid_webhook_id"}


async def test_second_instance_aborts(hass, hass_media_dir):
    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_USER}
    )
    result = await hass.config_entries.flow.async_configure(
        result["flow_id"],
        {CONF_WEBHOOK_ID: "jukeb", CONF_PLAYER: "media_player.kitchen"},
    )
    assert result["type"] == FlowResultType.CREATE_ENTRY
    await hass.async_block_till_done()

    result = await hass.config_entries.flow.async_init(
        DOMAIN, context={"source": config_entries.SOURCE_USER}
    )
    assert result["type"] == FlowResultType.ABORT
