# Tools

Scripts for maintaining the repository. Nothing here is needed to build or use a box.

| Script | What it does |
|---|---|
| `render_icon.py` | Renders [`docs/images/icon.svg`](../docs/images/icon.svg) into `custom_components/jukebox/brand/icon.png` (256 px) and `icon@2x.png` (512 px), the icon Home Assistant 2026.3+ and HACS show for the integration. |

Run from the repository root (needs [uv](https://docs.astral.sh/uv/)):

```bash
uv run --no-project --with cairosvg --with pillow tools/render_icon.py
```

Edit the SVG, never the PNGs. `home-assistant/tests/test_brand.py` checks the PNG sizes.
