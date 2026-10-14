import struct
from pathlib import Path

import pytest

BRAND = Path(__file__).resolve().parents[2] / "custom_components" / "jukebox" / "brand"


def _png_size(path: Path) -> tuple[int, int]:
    data = path.read_bytes()[:24]
    assert data[:8] == b"\x89PNG\r\n\x1a\n", f"{path.name} is not a PNG"
    return struct.unpack(">II", data[16:24])


# HA shows these instead of a missing-icon placeholder, and the HACS "brands" check needs icon.png.
@pytest.mark.parametrize(("name", "size"), [("icon.png", 256), ("icon@2x.png", 512)])
def test_brand_icon(name, size):
    assert _png_size(BRAND / name) == (size, size)
