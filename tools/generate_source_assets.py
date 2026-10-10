"""Generate standalone Minecraft standard source 3D models and textures (OBJ/MTL/PNG)."""
from __future__ import annotations
import argparse
from pathlib import Path
from PIL import Image

import sys
sys.path.insert(0, str(Path(__file__).parent))

from extract_mc_assets import (
    PARTS,
    build_steve_parts,
    build_cube_block,
    export_obj,
)

BLOCK_PALETTES = {
    "dirt": [
        (134, 96, 67, 255),
        (115, 81, 55, 255),
        (101, 71, 48, 255),
        (87, 60, 40, 255),
    ],
    "stone": [
        (125, 125, 125, 255),
        (110, 110, 110, 255),
        (95, 95, 95, 255),
        (80, 80, 80, 255),
    ],
    "tnt": [
        (219, 57, 36, 255),
        (190, 40, 25, 255),
        (240, 240, 240, 255),
        (30, 30, 30, 255),
    ],
    "diamond_sword": [
        (45, 221, 214, 255),
        (32, 179, 173, 255),
        (104, 76, 50, 255),
        (71, 51, 33, 255),
    ],
    "diamond_pickaxe": [
        (45, 221, 214, 255),
        (32, 179, 173, 255),
        (104, 76, 50, 255),
        (71, 51, 33, 255),
    ],
}


def generate_default_steve_skin() -> Image.Image:
    """Generate canonical 64x64 Steve skin with proper limb layout and colors."""
    img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))

    # Palettes
    skin_base = (195, 140, 105, 255)
    skin_dark = (175, 120, 85, 255)
    hair = (45, 25, 15, 255)
    eyes = (40, 70, 180, 255)
    whites = (240, 240, 240, 255)
    shirt = (0, 160, 170, 255)
    shirt_dark = (0, 130, 140, 255)
    pants = (40, 45, 130, 255)
    shoes = (65, 65, 70, 255)

    # Fill base colors across UV regions
    # Head: (0, 0) to (32, 16)
    for x in range(32):
        for y in range(16):
            img.putpixel((x, y), hair if y < 8 else skin_base)
    # Eyes
    img.putpixel((10, 10), whites)
    img.putpixel((11, 10), eyes)
    img.putpixel((13, 10), whites)
    img.putpixel((12, 10), eyes)

    # Torso: (16, 16) to (40, 32)
    for x in range(16, 40):
        for y in range(16, 32):
            img.putpixel((x, y), shirt if ((x + y) % 5 != 0) else shirt_dark)

    # Right Arm: (40, 16) to (56, 32)
    for x in range(40, 56):
        for y in range(16, 32):
            img.putpixel((x, y), shirt if y < 20 else skin_base)

    # Left Arm: (32, 48) to (48, 64)
    for x in range(32, 48):
        for y in range(48, 64):
            img.putpixel((x, y), shirt if y < 52 else skin_base)

    # Right Leg: (0, 16) to (16, 32)
    for x in range(16):
        for y in range(16, 32):
            img.putpixel((x, y), shoes if y >= 28 else pants)

    # Left Leg: (16, 48) to (32, 64)
    for x in range(16, 32):
        for y in range(48, 64):
            img.putpixel((x, y), shoes if y >= 60 else pants)

    return img


def generate_default_zombie_skin() -> Image.Image:
    """Generate canonical placeholder 64x64 Zombie skin with green skin, shirt, and mirrored limbs."""
    img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    skin_green = (55, 115, 45, 255)
    hair = (30, 60, 25, 255)
    eyes = (20, 20, 20, 255)
    shirt = (0, 150, 160, 255)
    shirt_dark = (0, 120, 130, 255)
    pants = (40, 45, 125, 255)
    shoes = (35, 40, 100, 255)

    # Head: (0, 0) to (32, 16)
    for x in range(32):
        for y in range(16):
            img.putpixel((x, y), hair if y < 8 else skin_green)
    # Eyes
    img.putpixel((10, 10), eyes)
    img.putpixel((11, 10), eyes)
    img.putpixel((12, 10), eyes)
    img.putpixel((13, 10), eyes)

    # Torso: (16, 16) to (40, 32)
    for x in range(16, 40):
        for y in range(16, 32):
            img.putpixel((x, y), shirt if ((x + y) % 5 != 0) else shirt_dark)

    # Right Arm: (40, 16) to (56, 32)
    for x in range(40, 56):
        for y in range(16, 32):
            img.putpixel((x, y), shirt if y < 20 else skin_green)

    # Right Leg: (0, 16) to (16, 32)
    for x in range(16):
        for y in range(16, 32):
            img.putpixel((x, y), pants if y < 28 else shoes)

    from extract_mc_assets import mirror_empty_left_limbs
    return mirror_empty_left_limbs(img)



def generate_block_texture(palette: list[tuple[int, int, int, int]]) -> Image.Image:
    """Generate a procedural 16x16 pixelated texture using palette seeds."""
    img = Image.new("RGBA", (16, 16), palette[0])
    p_len = len(palette)
    for y in range(16):
        for x in range(16):
            idx = (x * 7 + y * 13 + (x ^ y)) % p_len
            img.putpixel((x, y), palette[idx])
    return img


def export_mtl(material_name: str, texture_rel_path: str) -> str:
    return (
        f"newmtl {material_name}\n"
        "Ka 1.000 1.000 1.000\n"
        "Kd 1.000 1.000 1.000\n"
        "Ks 0.000 0.000 0.000\n"
        "d 1.0\n"
        "illum 1\n"
        f"map_Kd {texture_rel_path}\n"
    )


def generate_all_source_assets(output_dir: Path) -> dict[str, list[str]]:
    models_dir = output_dir / "models"
    textures_dir = output_dir / "textures"
    models_dir.mkdir(parents=True, exist_ok=True)
    textures_dir.mkdir(parents=True, exist_ok=True)

    manifest: dict[str, list[str]] = {"models": [], "textures": []}

    # 1. Generate Steve Skin & 12 Parts
    skin = generate_default_steve_skin()
    skin_path = textures_dir / "steve.png"
    skin.save(skin_path)
    manifest["textures"].append(str(skin_path))

    # 1b. Generate Zombie Skin
    zombie_skin = generate_default_zombie_skin()
    zombie_skin_path = textures_dir / "zombie.png"
    zombie_skin.save(zombie_skin_path)
    manifest["textures"].append(str(zombie_skin_path))

    steve_mtl_path = models_dir / "steve.mtl"
    steve_mtl_path.write_text(export_mtl("steve_mat", "../textures/steve.png"), encoding="utf-8")

    steve_parts = build_steve_parts(skin)
    for part in steve_parts:
        name = part["name"]
        obj_content = f"mtllib steve.mtl\nusemtl steve_mat\n" + export_obj(f"steve_{name}", part["mesh"])
        obj_path = models_dir / f"steve_{name}.obj"
        obj_path.write_text(obj_content, encoding="utf-8")
        manifest["models"].append(str(obj_path))

    # 2. Generate Blocks (Dirt, Stone, TNT)
    for block_name in ["dirt", "stone", "tnt"]:
        tex = generate_block_texture(BLOCK_PALETTES[block_name])
        tex_path = textures_dir / f"{block_name}.png"
        tex.save(tex_path)
        manifest["textures"].append(str(tex_path))

        mtl_path = models_dir / f"{block_name}.mtl"
        mtl_path.write_text(export_mtl(f"{block_name}_mat", f"../textures/{block_name}.png"), encoding="utf-8")

        cube_mesh = build_cube_block(tex, size_cm=100.0)
        obj_content = f"mtllib {block_name}.mtl\nusemtl {block_name}_mat\n" + export_obj(block_name, cube_mesh)
        obj_path = models_dir / f"{block_name}.obj"
        obj_path.write_text(obj_content, encoding="utf-8")
        manifest["models"].append(str(obj_path))

    # 3. Generate Items (Diamond Sword & Pickaxe flat cube approximations)
    for item_name in ["diamond_sword", "diamond_pickaxe"]:
        tex = generate_block_texture(BLOCK_PALETTES[item_name])
        tex_path = textures_dir / f"{item_name}.png"
        tex.save(tex_path)
        manifest["textures"].append(str(tex_path))

        mtl_path = models_dir / f"{item_name}.mtl"
        mtl_path.write_text(export_mtl(f"{item_name}_mat", f"../textures/{item_name}.png"), encoding="utf-8")

        item_mesh = build_cube_block(tex, size_cm=30.0) # smaller item scale
        obj_content = f"mtllib {item_name}.mtl\nusemtl {item_name}_mat\n" + export_obj(item_name, item_mesh)
        obj_path = models_dir / f"{item_name}.obj"
        obj_path.write_text(obj_content, encoding="utf-8")
        manifest["models"].append(str(obj_path))

    return manifest


def main():
    parser = argparse.ArgumentParser(description="Generate complete Minecraft 3D source assets.")
    parser.add_argument("--out-dir", type=Path, default=Path("assets/source"), help="Output directory")
    args = parser.parse_args()

    manifest = generate_all_source_assets(args.out_dir)
    print(f"Generated {len(manifest['models'])} models and {len(manifest['textures'])} textures in {args.out_dir}")


if __name__ == "__main__":
    main()
