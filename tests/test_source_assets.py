from __future__ import annotations
from pathlib import Path
import pytest
from PIL import Image

import sys
sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

from generate_source_assets import (
    generate_default_steve_skin,
    generate_block_texture,
    generate_all_source_assets,
    BLOCK_PALETTES
)


def test_generate_default_steve_skin():
    skin = generate_default_steve_skin()
    assert isinstance(skin, Image.Image)
    assert skin.size == (64, 64)
    assert skin.mode == "RGBA"
    # Steve skin has non-transparent pixels
    assert skin.getpixel((0, 0))[3] == 255


def test_generate_block_textures():
    for name, palette in BLOCK_PALETTES.items():
        tex = generate_block_texture(palette)
        assert tex.size == (16, 16)
        assert tex.mode == "RGBA"


def test_generate_all_source_assets(tmp_path: Path):
    out_dir = tmp_path / "assets"
    manifest = generate_all_source_assets(out_dir)

    assert "models" in manifest
    assert "textures" in manifest
    assert len(manifest["models"]) >= 15 # 12 steve parts + dirt + stone + tnt

    # Check files exist on disk
    for model_path in manifest["models"]:
        p = Path(model_path)
        assert p.exists()
        assert p.suffix == ".obj"
        assert p.stat().st_size > 0

    for tex_path in manifest["textures"]:
        p = Path(tex_path)
        assert p.exists()
        assert p.suffix == ".png"
        assert p.stat().st_size > 0
