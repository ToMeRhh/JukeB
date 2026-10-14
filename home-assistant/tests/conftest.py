"""Shared fixtures. `custom_components` must be imported before any HA fixture
runs, otherwise pytest-homeassistant-custom-component's own package shadows ours."""

import custom_components  # noqa: F401  (must stay first)

import pytest
from pytest_homeassistant_custom_component.common import MockConfigEntry

from custom_components.jukeb.const import CONF_PLAYER, CONF_WEBHOOK_ID, DOMAIN

PLAYER = "media_player.kitchen"


@pytest.fixture(autouse=True)
def auto_enable_custom_integrations(enable_custom_integrations):
    yield


@pytest.fixture
def hass_media_dir(hass, tmp_path):
    """Point HA's media directory at a temp folder named 'media' (like the user's HA)."""
    root = tmp_path / "media"
    root.mkdir()
    hass.config.media_dirs = {"media": str(root)}
    return root


@pytest.fixture
def config_entry(hass, hass_media_dir):
    entry = MockConfigEntry(
        domain=DOMAIN,
        title="JukeB",
        data={CONF_WEBHOOK_ID: "jukeb", CONF_PLAYER: PLAYER},
    )
    entry.add_to_hass(hass)
    return entry


@pytest.fixture
async def setup_jukeb(hass, config_entry):
    assert await hass.config_entries.async_setup(config_entry.entry_id)
    await hass.async_block_till_done()
    return config_entry
