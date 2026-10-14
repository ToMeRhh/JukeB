"""Render docs/images/icon.svg into the integration's brand/ PNGs.

Home Assistant (2026.3+) and HACS read custom_components/jukeb/brand/. Needs cairosvg and
Pillow: `uv run --no-project --with cairosvg --with pillow tools/render_icon.py` from the repo root.
"""

import io
from pathlib import Path

import cairosvg
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "docs" / "images" / "icon.svg"
BRAND = ROOT / "custom_components" / "jukeb" / "brand"
SIZES = {"icon.png": 256, "icon@2x.png": 512}


def main() -> None:
    BRAND.mkdir(exist_ok=True)
    for name, size in SIZES.items():
        png = cairosvg.svg2png(url=str(SOURCE), output_width=size, output_height=size)
        Image.open(io.BytesIO(png)).save(BRAND / name, optimize=True)
        print(f"wrote {BRAND / name} ({size}x{size})")


if __name__ == "__main__":
    main()
