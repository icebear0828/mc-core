#!/usr/bin/env python3
"""
Inspect the deployed mc_hud_atlas.png and check UV regions for:
1. item_shield
2. entity_shield
3. diamond armor icons
"""
from PIL import Image
from pathlib import Path

GAME_DIR = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game")
ATLAS_PATH = GAME_DIR / "mods" / "mc_adapter" / "mc_hud_atlas.png"

def check():
    if not ATLAS_PATH.is_file():
        print(f"Error: {ATLAS_PATH} not found")
        return
    img = Image.open(ATLAS_PATH).convert("RGBA")
    w, h = img.size
    print(f"Atlas dimensions: {w}x{h}")

    # UV definitions from include/mc/hud_atlas.hpp
    # kUV_ITEM_SHIELD = {0.908203f, 0.320312f, 0.939453f, 0.351562f};
    # kUV_ENTITY_SHIELD = {0.001953f, 0.369141f, 0.126953f, 0.494141f};
    # kUV_ITEM_DIAMOND_HELMET = {0.408203f, 0.320312f, 0.439453f, 0.351562f};
    # kUV_ITEM_DIAMOND_CHESTPLATE = {0.443359f, 0.320312f, 0.474609f, 0.351562f};
    # kUV_ITEM_DIAMOND_LEGGINGS = {0.478516f, 0.320312f, 0.509766f, 0.351562f};
    # kUV_ITEM_DIAMOND_BOOTS = {0.513672f, 0.320312f, 0.544922f, 0.351562f};

    regions = {
        "item_shield": (0.908203, 0.320312, 0.939453, 0.351562),
        "entity_shield": (0.001953, 0.369141, 0.126953, 0.494141),
        "diamond_helmet": (0.408203, 0.320312, 0.439453, 0.351562),
        "diamond_chestplate": (0.443359, 0.320312, 0.474609, 0.351562),
        "diamond_leggings": (0.478516, 0.320312, 0.509766, 0.351562),
        "diamond_boots": (0.513672, 0.320312, 0.544922, 0.351562),
    }

    for name, (u0, v0, u1, v1) in regions.items():
        x0 = int(round(u0 * w))
        y0 = int(round(v0 * h))
        x1 = int(round(u1 * w))
        y1 = int(round(v1 * h))
        box = (x0, y0, x1, y1)
        cropped = img.crop(box)
        pixels = list(cropped.getdata())
        non_transparent = [p for p in pixels if p[3] > 0]
        alpha_sum = sum(p[3] for p in pixels)
        print(f"Region {name} {box} ({x1-x0}x{y1-y0}): {len(non_transparent)}/{len(pixels)} solid pixels, total alpha: {alpha_sum}")

if __name__ == "__main__":
    check()
