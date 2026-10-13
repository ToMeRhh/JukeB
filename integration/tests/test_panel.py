import json
from pathlib import Path

from custom_components.jukebox.const import DOMAIN, VERSION


async def test_panel_registered_and_static_served(hass, setup_jukebox, hass_client):
    panels = hass.data["frontend_panels"]
    assert "jukebox" in panels
    panel = panels["jukebox"]
    assert panel.sidebar_title == "Jukebox"
    assert panel.sidebar_icon == "mdi:music-box-multiple"
    assert panel.require_admin is True
    assert panel.config["_panel_custom"]["name"] == "jukebox-panel"
    # The ?v= query is the only cache-buster browsers see: it must follow VERSION, and VERSION
    # must be bumped with every panel change.
    assert panel.config["_panel_custom"]["module_url"] == f"/api/jukebox/static/jukebox-panel.js?v={VERSION}"
    assert VERSION == "2.2.0"

    client = await hass_client()
    resp = await client.get("/api/jukebox/static/jukebox-panel.js")
    assert resp.status == 200
    text = await resp.text()
    assert 'customElements.define("jukebox-panel"' in text
    assert "import " not in text.split("\n", 1)[0]  # no external module imports


async def test_panel_removed_on_unload(hass, setup_jukebox):
    assert await hass.config_entries.async_unload(setup_jukebox.entry_id)
    await hass.async_block_till_done()
    assert "jukebox" not in hass.data["frontend_panels"]


async def test_reload_keeps_working(hass, setup_jukebox):
    assert await hass.config_entries.async_unload(setup_jukebox.entry_id)
    await hass.async_block_till_done()
    assert await hass.config_entries.async_setup(setup_jukebox.entry_id)
    await hass.async_block_till_done()
    assert "jukebox" in hass.data["frontend_panels"]
    assert DOMAIN in hass.data


def test_manifest_version_matches_const():
    manifest = json.loads((Path(__file__).parent.parent / "custom_components/jukebox/manifest.json").read_text())
    assert manifest["version"] == VERSION
