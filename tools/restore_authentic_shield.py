#!/usr/bin/env python3
"""
Formal deployment and verification script for restoring the authentic shield texture into Elden Ring.
Adheres strictly to the user instruction: "给我修复盾牌，其他不要动".
"""
from __future__ import annotations
import hashlib
import shutil
import sys
from pathlib import Path
from PIL import Image

def compute_sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def main() -> int:
    workspace_root = Path(__file__).resolve().parent.parent
    game_dir = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game")
    mods_dir = game_dir / "mods" / "mc_adapter"
    atlas_source = workspace_root / "build" / "authentic_atlas" / "mc_hud_atlas.png"

    if not atlas_source.is_file():
        print(f"ERROR: Source atlas not found: {atlas_source}", file=sys.stderr)
        return 1

    # Deep verification of entity_shield in atlas_source
    img = Image.open(atlas_source).convert("RGBA")
    w, h = img.size
    assert (w, h) == (512, 512), f"Expected 512x512 atlas, got {w}x{h}"
    shield_box = (1, 189, 65, 253)
    shield_crop = img.crop(shield_box)
    solid_shield_pixels = sum(1 for p in shield_crop.getdata() if p[3] > 0)
    print(f"Source entity_shield solid pixels: {solid_shield_pixels} / 4096")
    if solid_shield_pixels != 716:
        print(f"ERROR: entity_shield solid pixels ({solid_shield_pixels}) does not match authentic jar (716)!", file=sys.stderr)
        return 1

    dst_game_atlas = game_dir / "mc_hud_atlas.png"
    dst_mods_atlas = mods_dir / "mc_hud_atlas.png"

    print("Deploying authentic shield-restored atlas...")
    shutil.copy2(atlas_source, dst_game_atlas)
    shutil.copy2(atlas_source, dst_mods_atlas)

    # Hash verification
    src_hash = compute_sha256(atlas_source)
    dst_game_hash = compute_sha256(dst_game_atlas)
    dst_mods_hash = compute_sha256(dst_mods_atlas)

    print(f"Source SHA256    : {src_hash}")
    print(f"Game Atlas SHA256: {dst_game_hash}")
    print(f"Mods Atlas SHA256: {dst_mods_hash}")

    assert src_hash == dst_game_hash == dst_mods_hash, "Hash mismatch!"
    print("[PASS] Authentic shield texture deployed with 100% integrity.")
    return 0

if __name__ == "__main__":
    sys.exit(main())
