from __future__ import annotations
import io
from pathlib import Path
import zipfile
import pytest
from PIL import Image

import sys
sys.path.insert(0, str(Path(__file__).parent.parent / "tools"))

from extract_mc_assets import (
    HUD_SPRITES,
    _JAR_HUD_SPRITES,
    build_hud_atlas,
    create_canonical_sprite,
)

JAR_PATH = Path(r"D:\game\sekiro\build\mc_client_1.21.8.jar")


def test_destroy_stages_defined_in_hud_sprites():
    """Verify all 10 destroy stages (0-9) are registered in HUD_SPRITES with 16x16 dimensions."""
    registered = dict(HUD_SPRITES)
    for i in range(10):
        key = f"destroy_stage_{i}"
        assert key in registered, f"{key} must be defined in HUD_SPRITES"
        assert registered[key] == (16, 16), f"{key} must be 16x16"
        assert key in _JAR_HUD_SPRITES, f"{key} must be mapped in _JAR_HUD_SPRITES"
        assert _JAR_HUD_SPRITES[key] == f"block/destroy_stage_{i}.png"


def test_destroy_stages_canonical_placeholder_progression():
    """Verify canonical placeholders for destroy stages show strictly progressive crack density."""
    prev_active_pixels = 0
    for i in range(10):
        sprite = create_canonical_sprite(f"destroy_stage_{i}")
        assert sprite.size == (16, 16)
        active_pixels = sum(1 for y in range(16) for x in range(16) if sprite.getpixel((x, y))[3] > 0)
        assert active_pixels > prev_active_pixels, (
            f"Stage {i} crack pixel count ({active_pixels}) must be strictly greater than "
            f"stage {i-1} ({prev_active_pixels})"
        )
        prev_active_pixels = active_pixels


def test_destroy_stages_authentic_jar_extraction():
    """Verify authentic Mojang client.jar contains all 10 authentic destroy_stage textures with strictly progressive crack density."""
    assert JAR_PATH.exists(), f"Client jar must exist at {JAR_PATH}"
    with zipfile.ZipFile(JAR_PATH, "r") as zf:
        namelist = set(zf.namelist())
        prev_crack_count = 0
        for i in range(10):
            jar_entry = f"assets/minecraft/textures/block/destroy_stage_{i}.png"
            assert jar_entry in namelist, f"{jar_entry} must exist in vanilla client.jar"
            with zf.open(jar_entry) as f:
                rgba = Image.open(f).convert("RGBA")
                assert rgba.size == (16, 16), f"Stage {i} must be 16x16"
                alphas = [rgba.getpixel((x, y))[3] for y in range(16) for x in range(16)]
                crack_count = alphas.count(255)
                assert crack_count > prev_crack_count, (
                    f"Stage {i} authentic crack pixel count ({crack_count}) must be strictly greater than "
                    f"stage {i-1} ({prev_crack_count})"
                )
                prev_crack_count = crack_count





def test_atlas_uv_mapping_for_destroy_stages():
    """Verify atlas packs all 10 stages with valid, non-overlapping UV bounds."""
    atlas, uv_map = build_hud_atlas(JAR_PATH if JAR_PATH.exists() else None)
    assert atlas.size == (512, 512)
    for i in range(10):
        key = f"destroy_stage_{i}"
        assert key in uv_map, f"{key} must exist in atlas UV map"
        u0, v0, u1, v1 = uv_map[key]
        assert 0.0 <= u0 < u1 <= 1.0
        assert 0.0 <= v0 < v1 <= 1.0
        # 16 pixels in a 512 texture is 16/512 = 0.03125
        assert pytest.approx(u1 - u0, abs=1e-5) == 16.0 / 512.0
        assert pytest.approx(v1 - v0, abs=1e-5) == 16.0 / 512.0


LOOT_ICON_KEYS = [
    "item_coal",
    "item_raw_copper",
    "item_raw_iron",
    "item_redstone",
    "item_raw_gold",
    "item_lapis_lazuli",
    "item_amethyst_shard",
    "item_diamond",
    "item_emerald",
    "item_netherite_scrap",
    "item_cobblestone",
]


def test_loot_icons_registered_in_atlas():
    """Issue #81 loot items must have a 16x16 icon and a client.jar source."""
    registered = dict(HUD_SPRITES)
    for key in LOOT_ICON_KEYS:
        assert key in registered, f"{key} must be defined in HUD_SPRITES"
        assert registered[key] == (16, 16), f"{key} must be 16x16"
        assert key in _JAR_HUD_SPRITES, f"{key} must be mapped in _JAR_HUD_SPRITES"


def test_loot_icons_have_atlas_cells():
    atlas, uv_map = build_hud_atlas(JAR_PATH if JAR_PATH.exists() else None)
    assert atlas.size == (512, 512)
    for key in LOOT_ICON_KEYS:
        assert key in uv_map, f"{key} must exist in atlas UV map"
        u0, v0, u1, v1 = uv_map[key]
        assert 0.0 <= u0 < u1 <= 1.0
        assert 0.0 <= v0 < v1 <= 1.0
        assert pytest.approx(u1 - u0, abs=1e-5) == 16.0 / 512.0
        assert pytest.approx(v1 - v0, abs=1e-5) == 16.0 / 512.0
