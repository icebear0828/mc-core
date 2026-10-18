#!/usr/bin/env python3
"""
Inspect mob textures and UV coordinates to detect any zero-alpha or clipping issues.
"""
import json
from pathlib import Path
from PIL import Image

GAME_DIR = Path(r"C:\Program Files (x86)\Steam\steamapps\common\ELDEN RING\Game")
MOD_DIR = GAME_DIR / "mods" / "mc_adapter"

def inspect_mobs():
    print("=== Mob Texture & Model Inspection ===")
    for mob in ["creeper", "zombie", "skeleton"]:
        tex_path = MOD_DIR / f"{mob}.png"
        geo_path = MOD_DIR / "models" / f"{mob}.geo.json"
        print(f"\n--- Checking {mob} ---")
        if not tex_path.is_file():
            print(f"[ERROR] {tex_path} does not exist!")
            continue
        if not geo_path.is_file():
            print(f"[ERROR] {geo_path} does not exist!")
            continue

        img = Image.open(tex_path).convert("RGBA")
        tw, th = img.size
        print(f"Texture {tex_path.name}: {tw}x{th}")

        with open(geo_path, "r", encoding="utf-8") as f:
            geo = json.load(f)

        # Parse bones and cubes
        geoms = geo.get("minecraft:geometry", [])
        if not geoms:
            print("[ERROR] No minecraft:geometry found")
            continue
        g = geoms[0]
        desc = g.get("description", {})
        mtw = desc.get("texture_width", 64)
        mth = desc.get("texture_height", 64)
        print(f"Model declared texture size: {mtw}x{mth}")
        if (tw, th) != (mtw, mth):
            print(f"[WARNING] Size mismatch! Texture is {tw}x{th}, model expects {mtw}x{mth}")

        bones = g.get("bones", [])
        print(f"Total bones: {len(bones)}")
        for b in bones:
            bname = b.get("name", "unnamed")
            cubes = b.get("cubes", [])
            for ci, c in enumerate(cubes):
                u, v = c.get("uv", [0, 0])
                sx, sy, sz = c.get("size", [0, 0, 0])
                # Check UV rectangle bounds in texture
                # Box faces:
                # East: [u, v+sz, sz, sy]
                # North: [u+sz, v+sz, sx, sy]
                # West: [u+sz+sx, v+sz, sz, sy]
                # South: [u+2*sz+sx, v+sz, sx, sy]
                # Top: [u+sz, v, sx, sz]
                # Bottom: [u+sz+sx, v, sx, sz]
                faces = {
                    "top": (u + sz, v, sx, sz),
                    "bottom": (u + sz + sx, v, sx, sz),
                    "north": (u + sz, v + sz, sx, sy),
                    "south": (u + 2 * sz + sx, v + sz, sx, sy),
                    "west": (u + sz + sx, v + sz, sz, sy),
                    "east": (u, v + sz, sz, sy),
                }
                for fname, (fx, fy, fw, fh) in faces.items():
                    # Check if face is outside image
                    if fx + fw > tw or fy + fh > th:
                        print(f"  [ERROR] {bname} cube {ci} face {fname} out of bounds: ({fx}, {fy}, {fw}, {fh}) > {tw}x{th}!")
                    else:
                        crop = img.crop((int(fx), int(fy), int(fx + fw), int(fy + fh)))
                        alphas = [p[3] for p in crop.getdata()]
                        zero_alphas = sum(1 for a in alphas if a < 128)
                        if zero_alphas == len(alphas):
                            print(f"  [WARN] {bname} cube {ci} face {fname} is 100% TRANSPARENT! ({zero_alphas}/{len(alphas)} pixels alpha < 128)")

if __name__ == "__main__":
    inspect_mobs()
