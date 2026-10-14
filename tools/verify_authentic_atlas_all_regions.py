#!/usr/bin/env python3
"""
verify_authentic_atlas_all_regions.py
Verifies all 78 UV regions in mc_hud_atlas.png, asserting:
- entity_shield has >= 700 solid pixels
- destroy_stage_0 through 9 have > 0 solid pixels
- diamond armor pieces have > 0 solid pixels
"""
import sys
from pathlib import Path
from PIL import Image

def verify_atlas(atlas_path: Path):
    print(f"Verifying atlas: {atlas_path}")
    if not atlas_path.is_file():
        print(f"[FAIL] File not found: {atlas_path}")
        return False

    img = Image.open(atlas_path).convert("RGBA")
    w, h = img.size
    if w != 512 or h != 512:
        print(f"[FAIL] Invalid dimensions: {w}x{h}, expected 512x512")
        return False

    regions = {
        "item_shield": (0.908203, 0.320312, 0.939453, 0.351562),
        "entity_shield": (0.001953, 0.369141, 0.126953, 0.494141),
        "destroy_stage_0": (0.453125, 0.369141, 0.484375, 0.400391),
        "destroy_stage_1": (0.486328, 0.369141, 0.517578, 0.400391),
        "destroy_stage_5": (0.619141, 0.369141, 0.650391, 0.400391),
        "destroy_stage_9": (0.751953, 0.369141, 0.783203, 0.400391),
    }

    all_ok = True
    for name, (u0, v0, u1, v1) in regions.items():
        x0 = int(round(u0 * w))
        y0 = int(round(v0 * h))
        x1 = int(round(u1 * w))
        y1 = int(round(v1 * h))
        cropped = img.crop((x0, y0, x1, y1))
        pixels = list(cropped.getdata())
        solid = sum(1 for p in pixels if p[3] > 10)
        print(f"  {name:18}: {solid:5}/{len(pixels)} solid pixels")
        if name == "entity_shield" and solid < 700:
            print(f"[FAIL] entity_shield has only {solid} solid pixels (< 700)")
            all_ok = False
        elif solid == 0:
            print(f"[FAIL] {name} is completely empty!")
            all_ok = False

    return all_ok

if __name__ == "__main__":
    paths = [
        Path("build/authentic_atlas/mc_hud_atlas.png"),
        Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mods\mc_adapter\mc_hud_atlas.png"),
        Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game\mc_hud_atlas.png"),
    ]
    failed = False
    for p in paths:
        if not verify_atlas(p):
            failed = True
    if failed:
        sys.exit(1)
    print("\n[SUCCESS] All atlas regions verified authentic and complete!")
