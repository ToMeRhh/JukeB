import json
from pathlib import Path

from custom_components.jukeb.const import DOMAIN, VERSION


async def test_panel_registered_and_static_served(hass, setup_jukeb, hass_client):
    panels = hass.data["frontend_panels"]
    assert "jukeb" in panels
    panel = panels["jukeb"]
    assert panel.sidebar_title == "JukeB"
    assert panel.sidebar_icon == "mdi:music-box-multiple"
    assert panel.require_admin is True
    assert panel.config["_panel_custom"]["name"] == "jukeb-panel"
    # The ?v= query is the only cache-buster browsers see: it must follow VERSION, and VERSION
    # must be bumped with every panel change.
    assert panel.config["_panel_custom"]["module_url"] == f"/api/jukeb/static/jukeb-panel.js?v={VERSION}"
    assert VERSION == "3.1.0"

    client = await hass_client()
    resp = await client.get("/api/jukeb/static/jukeb-panel.js")
    assert resp.status == 200
    text = await resp.text()
    assert 'customElements.define("jukeb-panel"' in text
    assert "import " not in text.split("\n", 1)[0]  # no external module imports


async def test_panel_removed_on_unload(hass, setup_jukeb):
    assert await hass.config_entries.async_unload(setup_jukeb.entry_id)
    await hass.async_block_till_done()
    assert "jukeb" not in hass.data["frontend_panels"]


async def test_reload_keeps_working(hass, setup_jukeb):
    assert await hass.config_entries.async_unload(setup_jukeb.entry_id)
    await hass.async_block_till_done()
    assert await hass.config_entries.async_setup(setup_jukeb.entry_id)
    await hass.async_block_till_done()
    assert "jukeb" in hass.data["frontend_panels"]
    assert DOMAIN in hass.data


def test_manifest_version_matches_const():
    manifest = json.loads((Path(__file__).parent.parent.parent / "custom_components/jukeb/manifest.json").read_text())
    assert manifest["version"] == VERSION
