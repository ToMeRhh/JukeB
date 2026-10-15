import json
from pathlib import Path

from custom_components.jukeb.const import DOMAIN

ROOT = Path(__file__).parent.parent.parent


def test_hacs_installs_the_release_zip():
    # HACS downloads this asset from each release (release.yml attaches it), and GitHub counts
    # those downloads; the zip holds the contents of custom_components/<domain>/.
    hacs = json.loads((ROOT / "hacs.json").read_text())
    assert hacs["zip_release"] is True
    assert hacs["filename"] == f"{DOMAIN}.zip"
    assert f"{DOMAIN}.zip" in (ROOT / ".github/workflows/release.yml").read_text()
