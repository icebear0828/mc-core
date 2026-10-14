#!/usr/bin/env python3
"""
Ensures authentic Minecraft textures are in place:
1. Pastes authentic creeper_spawn_egg into authentic mc_hud_atlas.
2. Deploys authentic atlas to Game/mc_hud_atlas.png and Game/mods/mc_adapter/mc_hud_atlas.png.
3. Deploys authentic creeper.png to Game/mods/mc_adapter/creeper.png.
"""
from pathlib import Path
from PIL import Image
import shutil

ROOT = Path(__file__).resolve().parent.parent
GAME_DIR = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game")
MODS_DIR = GAME_DIR / "mods" / "mc_adapter"

def main():
    atlas_auth = ROOT / "assets" / "source" / "textures" / "mc_hud_atlas.authentic.png"
    egg_auth = ROOT / "assets" / "source" / "textures" / "creeper_spawn_egg.authentic.png"
    creeper_auth = ROOT / "assets" / "source" / "models" / "entities" / "creeper.authentic.png"

    if not atlas_auth.is_file():
        raise FileNotFoundError(f"Missing {atlas_auth}")
    if not egg_auth.is_file():
        raise FileNotFoundError(f"Missing {egg_auth}")
    if not creeper_auth.is_file():
        raise FileNotFoundError(f"Missing {creeper_auth}")

    print("=== Updating Authentic Minecraft Textures ===")
    atlas_img = Image.open(atlas_auth).convert("RGBA")
    egg_img = Image.open(egg_auth).convert("RGBA")

    # Spawn egg slot is at (367, 164) width 16, height 16
    egg_box = (367, 164)
    # Clear background in the 16x16 slot first
    blank = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    atlas_img.paste(blank, egg_box)
    atlas_img.alpha_composite(egg_img, egg_box)

    # Save updated authentic atlas
    atlas_img.save(atlas_auth)
    print(f"[OK] Updated authentic atlas at {atlas_auth}")

    # Deploy to game directories
    dest_game_atlas = GAME_DIR / "mc_hud_atlas.png"
    dest_mods_atlas = MODS_DIR / "mc_hud_atlas.png"
    dest_mods_auth_atlas = MODS_DIR / "mc_hud_atlas.authentic.png"

    atlas_img.save(dest_game_atlas)
    print(f"[DEPLOY] Saved authentic atlas -> {dest_game_atlas}")
    atlas_img.save(dest_mods_atlas)
    print(f"[DEPLOY] Saved authentic atlas -> {dest_mods_atlas}")
    atlas_img.save(dest_mods_auth_atlas)
    print(f"[DEPLOY] Saved authentic atlas -> {dest_mods_auth_atlas}")

    # Deploy authentic creeper.png
    dest_creeper = MODS_DIR / "creeper.png"
    shutil.copy2(creeper_auth, dest_creeper)
    print(f"[DEPLOY] Deployed authentic creeper skin (64x32 Java) -> {dest_creeper}")

    print("=== Authentic Textures Update Complete! ===")

if __name__ == "__main__":
    main()
