"""Extract Minecraft Java assets into standard OBJ/PNG 3D models for cross-engine importing."""
from __future__ import annotations
import argparse
import io
import math
from pathlib import Path
import zipfile
from PIL import Image

PARTS = [
    ("head", (0, 0, 0), (-4, -8, -4), (8, 8, 8), (0, 0), 0),
    ("body", (0, 0, 0), (-4, 0, -2), (8, 12, 4), (16, 16), 0),
    ("right_arm", (-5, 2, 0), (-3, -2, -2), (4, 12, 4), (40, 16), 0),
    ("left_arm", (5, 2, 0), (-1, -2, -2), (4, 12, 4), (32, 48), 0),
    ("right_leg", (-1.9, 12, 0), (-2, 0, -2), (4, 12, 4), (0, 16), 0),
    ("left_leg", (1.9, 12, 0), (-2, 0, -2), (4, 12, 4), (16, 48), 0),
    ("hat", (0, 0, 0), (-4, -8, -4), (8, 8, 8), (32, 0), 0.5),
    ("jacket", (0, 0, 0), (-4, 0, -2), (8, 12, 4), (16, 32), 0.25),
    ("right_sleeve", (-5, 2, 0), (-3, -2, -2), (4, 12, 4), (40, 32), 0.25),
    ("left_sleeve", (5, 2, 0), (-1, -2, -2), (4, 12, 4), (48, 48), 0.25),
    ("right_pants", (-1.9, 12, 0), (-2, 0, -2), (4, 12, 4), (0, 32), 0.25),
    ("left_pants", (1.9, 12, 0), (-2, 0, -2), (4, 12, 4), (0, 48), 0.25),
]


def cuboid(origin: tuple[float, float, float], size: tuple[float, float, float],
           uv: tuple[float, float], inflate: float = 0.0) -> list:
    x, y, z = origin
    w, h, d = size
    x0, y0, z0 = x - inflate, y - inflate, z - inflate
    x1, y1, z1 = x + w + inflate, y + h + inflate, z + d + inflate
    u, v = uv

    # Unfolded cuboid texture faces: outward-facing quads
    faces = [
        ([(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)], (u + d, v + d, w, h)),
        ([(x1, y0, z1), (x0, y0, z1), (x0, y1, z1), (x1, y1, z1)], (u + d + w + d, v + d, w, h)),
        ([(x1, y0, z0), (x1, y0, z1), (x1, y1, z1), (x1, y1, z0)], (u + d + w, v + d, d, h)),
        ([(x0, y0, z1), (x0, y0, z0), (x0, y1, z0), (x0, y1, z1)], (u, v + d, d, h)),
        ([(x0, y0, z1), (x1, y0, z1), (x1, y0, z0), (x0, y0, z0)], (u + d, v, w, d)),
        ([(x0, y1, z0), (x1, y1, z0), (x1, y1, z1), (x0, y1, z1)], (u + d + w, v + d, w, -d)),
    ]
    return faces


def mesh_from_faces(faces: list, texture: Image.Image) -> dict:
    positions, normals, uvs, colors, triangles = [], [], [], [], []
    tw, th = texture.size

    for verts, (u, v, w, h) in faces:
        cols, rows = int(abs(w)), int(abs(h))
        for iy in range(rows):
            for ix in range(cols):
                px = math.floor(u + (ix + 0.5) * w / cols) % tw
                py = math.floor(v + (iy + 0.5) * h / rows) % th
                rgba = texture.getpixel((px, py))
                if rgba[3] == 0:
                    continue

                base = len(positions)
                for s, t in [(ix / cols, iy / rows), ((ix + 1) / cols, iy / rows),
                             ((ix + 1) / cols, (iy + 1) / rows), (ix / cols, (iy + 1) / rows)]:
                    pos = [
                        verts[0][j] + s * (verts[1][j] - verts[0][j]) + t * (verts[3][j] - verts[0][j])
                        for j in range(3)
                    ]
                    positions.append(pos)
                    uvs.append([(u + s * w) / tw, 1.0 - (v + t * h) / th]) # OBJ flip V
                    colors.append([c / 255.0 for c in rgba])

                # Calculate outward normal
                a = [positions[base + 1][j] - positions[base][j] for j in range(3)]
                b = [positions[base + 3][j] - positions[base][j] for j in range(3)]
                n = [
                    a[1] * b[2] - a[2] * b[1],
                    a[2] * b[0] - a[0] * b[2],
                    a[0] * b[1] - a[1] * b[0],
                ]
                length = math.sqrt(sum(c * c for c in n)) or 1.0
                norm = [-c / length for c in n]
                normals.extend([norm] * 4)

                triangles.extend([base, base + 2, base + 1, base, base + 3, base + 2])

    return {
        "positions": positions,
        "normals": normals,
        "uv": uvs,
        "colors": colors,
        "triangles": triangles,
    }


def export_obj(name: str, mesh: dict) -> str:
    lines = [f"# Model: {name}", f"o {name}"]

    for x, y, z in mesh["positions"]:
        lines.append(f"v {x:.4f} {y:.4f} {z:.4f}")

    for u, v in mesh["uv"]:
        lines.append(f"vt {u:.4f} {v:.4f}")

    for nx, ny, nz in mesh["normals"]:
        lines.append(f"vn {nx:.4f} {ny:.4f} {nz:.4f}")

    # OBJ indices are 1-based (v/vt/vn)
    triangles = mesh["triangles"]
    for i in range(0, len(triangles), 3):
        i0, i1, i2 = triangles[i] + 1, triangles[i + 1] + 1, triangles[i + 2] + 1
        lines.append(f"f {i0}/{i0}/{i0} {i1}/{i1}/{i1} {i2}/{i2}/{i2}")

    return "\n".join(lines) + "\n"


def build_steve_parts(skin_texture: Image.Image) -> list[dict]:
    parts = []
    for name, pivot, origin, size, uv, inflate in PARTS:
        faces = cuboid(origin, size, uv, inflate=inflate)
        mesh = mesh_from_faces(faces, skin_texture)
        parts.append({"name": name, "pivot": pivot, "mesh": mesh})
    return parts


def build_cube_block(texture: Image.Image, size_cm: float = 100.0) -> dict:
    h = size_cm * 0.5
    # 6 outward facing quads for standard game engine cube
    face_defs = [
        # South (-Y)
        ([(h, -h, -h), (-h, -h, -h), (-h, -h, h), (h, -h, h)], (0.0, -1.0, 0.0)),
        # North (+Y)
        ([(-h, h, -h), (h, h, -h), (h, h, h), (-h, h, h)], (0.0, 1.0, 0.0)),
        # East (+X)
        ([(h, h, -h), (h, -h, -h), (h, -h, h), (h, h, h)], (1.0, 0.0, 0.0)),
        # West (-X)
        ([(-h, -h, -h), (-h, h, -h), (-h, h, h), (-h, -h, h)], (-1.0, 0.0, 0.0)),
        # Top (+Z)
        ([(-h, -h, h), (h, -h, h), (h, h, h), (-h, h, h)], (0.0, 0.0, 1.0)),
        # Bottom (-Z)
        ([(-h, h, -h), (h, h, -h), (h, -h, -h), (-h, -h, -h)], (0.0, 0.0, -1.0)),
    ]

    positions, normals, uvs, colors, triangles = [], [], [], [], []
    quad_uv = [(0.0, 0.0), (1.0, 0.0), (1.0, 1.0), (0.0, 1.0)]

    for verts, norm in face_defs:
        base = len(positions)
        for i, pos in enumerate(verts):
            positions.append([float(pos[0]), float(pos[1]), float(pos[2])])
            normals.append([float(norm[0]), float(norm[1]), float(norm[2])])
            uvs.append(list(quad_uv[i]))
            colors.append([1.0, 1.0, 1.0, 1.0])
        triangles.extend([base, base + 1, base + 2, base, base + 2, base + 3])

    return {
        "positions": positions,
        "normals": normals,
        "uv": uvs,
        "colors": colors,
        "triangles": triangles,
    }


_PLACEHOLDER_ITEM_COLORS = {
    "item_arrow": (200, 200, 200), "item_trident": (40, 150, 150), "item_flint_and_steel": (90, 90, 90),
    "item_ender_pearl": (20, 90, 80), "item_enchanted_golden_apple": (200, 120, 230), "item_bread": (200, 150, 70),
    "item_cooked_beef": (120, 60, 30), "item_firework_rocket": (200, 40, 40),
    "block_dirt": (134, 96, 67), "block_stone": (125, 125, 125), "block_tnt_top": (160, 80, 70),
    "block_tnt_side": (200, 60, 50), "block_tnt_bottom": (160, 80, 70),
}


def create_canonical_sprite(name: str) -> Image.Image:
    """Generate canonical pixel sprite when extracting without an official client.jar."""
    if name == "crosshair":
        img = Image.new("RGBA", (15, 15), (0, 0, 0, 0))
        for i in range(4, 11):  # a plain white plus; the real sprite replaces it when a client.jar is given
            img.putpixel((7, i), (255, 255, 255, 220))
            img.putpixel((i, 7), (255, 255, 255, 220))
        return img

    if name in ("heart_container", "heart_half", "hunger_container", "hunger_half"):
        # Derived from the full sprite so the placeholder set stays visually consistent
        base = create_canonical_sprite("heart_full" if name.startswith("heart") else "hunger_full")
        out = Image.new("RGBA", base.size, (0, 0, 0, 0))
        for y in range(base.height):
            for x in range(base.width):
                r, g, b, a = base.getpixel((x, y))
                if a == 0:
                    continue
                if name.endswith("container"):
                    out.putpixel((x, y), (r, g, b, a) if (r, g, b) == (0, 0, 0) else (50, 50, 50, 255))
                elif x < base.width // 2 + 1:
                    out.putpixel((x, y), (r, g, b, a))
        return out

    if name == "heart_full":
        img = Image.new("RGBA", (9, 9), (0, 0, 0, 0))
        c_blk, c_red, c_wht, c_drk = (0, 0, 0, 255), (235, 18, 18, 255), (255, 255, 255, 255), (145, 0, 0, 255)
        pixels = {
            (1, 0): c_blk, (2, 0): c_blk, (5, 0): c_blk, (6, 0): c_blk,
            (0, 1): c_blk, (1, 1): c_red, (2, 1): c_wht, (3, 1): c_blk, (4, 1): c_blk, (5, 1): c_red, (6, 1): c_red, (7, 1): c_blk,
            (0, 2): c_blk, (1, 2): c_wht, (2, 2): c_red, (3, 2): c_red, (4, 2): c_red, (5, 2): c_red, (6, 2): c_drk, (7, 2): c_blk,
            (0, 3): c_blk, (1, 3): c_red, (2, 3): c_red, (3, 3): c_red, (4, 3): c_red, (5, 3): c_red, (6, 3): c_drk, (7, 3): c_blk,
            (1, 4): c_blk, (2, 4): c_red, (3, 4): c_red, (4, 4): c_red, (5, 4): c_drk, (6, 4): c_blk,
            (2, 5): c_blk, (3, 5): c_red, (4, 5): c_drk, (5, 5): c_blk,
            (3, 6): c_blk, (4, 6): c_drk, (5, 6): c_blk,
            (4, 7): c_blk,
        }
        for (x, y), col in pixels.items():
            img.putpixel((x, y), col)
        return img

    if name == "hunger_full":
        img = Image.new("RGBA", (9, 9), (0, 0, 0, 0))
        c_blk, c_tan, c_car, c_drk = (35, 15, 5, 255), (225, 149, 79, 255), (189, 107, 34, 255), (109, 57, 14, 255)
        c_bnw, c_bnd = (230, 230, 230, 255), (160, 160, 160, 255)
        pixels = {
            (4, 0): c_blk, (5, 0): c_blk, (6, 0): c_blk,
            (3, 1): c_blk, (4, 1): c_tan, (5, 1): c_car, (6, 1): c_car, (7, 1): c_blk,
            (2, 2): c_blk, (3, 2): c_tan, (4, 2): c_car, (5, 2): c_car, (6, 2): c_drk, (7, 2): c_blk,
            (1, 3): c_blk, (2, 3): c_car, (3, 3): c_car, (4, 3): c_car, (5, 3): c_drk, (6, 3): c_drk, (7, 3): c_blk,
            (0, 4): c_blk, (1, 4): c_car, (2, 4): c_car, (3, 4): c_car, (4, 4): c_drk, (5, 4): c_blk,
            (0, 5): c_blk, (1, 5): c_bnw, (2, 5): c_car, (3, 5): c_drk, (4, 5): c_blk,
            (1, 6): c_blk, (2, 6): c_bnd, (3, 6): c_bnw, (4, 6): c_blk,
            (1, 7): c_blk, (2, 7): c_bnw, (3, 7): c_bnd, (4, 7): c_blk,
            (2, 8): c_blk, (3, 8): c_blk,
        }
        for (x, y), col in pixels.items():
            img.putpixel((x, y), col)
        return img

    if name in ("heart_absorb_full", "heart_absorb_half"):
        base = create_canonical_sprite("heart_full" if name.endswith("full") else "heart_half")
        out = Image.new("RGBA", base.size, (0, 0, 0, 0))
        recolor = {(235, 18, 18): (255, 205, 40), (145, 0, 0): (170, 120, 0)}
        for y in range(base.height):
            for x in range(base.width):
                r, g, b, a = base.getpixel((x, y))
                if a:
                    nr, ng, nb = recolor.get((r, g, b), (r, g, b))
                    out.putpixel((x, y), (nr, ng, nb, a))
        return out

    if name == "particle_crit":
        img = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
        for i in range(8):  # a plain four-point star
            img.putpixel((3, i), (255, 255, 255, 255))
            img.putpixel((4, i), (255, 255, 255, 255))
            img.putpixel((i, 3), (255, 255, 255, 255))
            img.putpixel((i, 4), (255, 255, 255, 255))
        return img

    if name == "particle_damage":
        img = Image.new("RGBA", (8, 8), (0, 0, 0, 0))
        for (x, y) in [(1, 2), (2, 1), (5, 1), (6, 2), (1, 3), (2, 3), (3, 3), (4, 3), (5, 3), (6, 3), (2, 4), (3, 4), (4, 4), (5, 4), (3, 5), (4, 5)]:
            img.putpixel((x, y), (130, 0, 0, 255))
        return img

    if name.startswith("particle_sweep_"):
        idx = int(name.rsplit("_", 1)[1])
        img = Image.new("RGBA", (32, 32), (0, 0, 0, 0))
        reach = 4 + idx * 3  # an arc that grows with the frame; the real frames replace it
        for x in range(32):
            for y in range(32):
                d = ((x - 16) ** 2 + (y - 26) ** 2) ** 0.5
                if reach - 1.5 <= d <= reach + 1.5 and y < 26:
                    img.putpixel((x, y), (255, 255, 255, 255 - idx * 20))
        return img

    if name in ("container_top", "container_bottom"):
        h = 71 if name == "container_top" else 96
        img = Image.new("RGBA", (176, h), (198, 198, 198, 255))
        rows = [(8 + 0, 18 + 18 * r) for r in range(3)] if name == "container_top" else [(8, 14 + 18 * r) for r in range(3)] + [(8, 72)]
        for sx, sy in rows:
            for c in range(9):
                for dx in range(-1, 17):
                    for dy in range(-1, 17):
                        x, y = sx + 18 * c + dx, sy + dy
                        if 0 <= x < 176 and 0 <= y < h:
                            img.putpixel((x, y), (139, 139, 139, 255))
        return img

    if name == "hotbar":
        img = Image.new("RGBA", (182, 22), (40, 40, 40, 200))
        for x in range(182):
            for y in (0, 21):
                img.putpixel((x, y), (200, 200, 200, 255))
        for i in range(10):
            for y in range(22):
                img.putpixel((min(i * 20 + 1, 181), y), (200, 200, 200, 255))
        return img

    if name == "hotbar_selection":
        img = Image.new("RGBA", (24, 23), (0, 0, 0, 0))
        for x in range(24):
            for y in range(23):
                if x < 2 or x >= 22 or y < 2 or y >= 21:
                    img.putpixel((x, y), (255, 255, 255, 255))
        return img

    # 16x16 Items
    img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    if name == "item_diamond_sword":
        c_dm, c_dd, c_hi = (43, 235, 235, 255), (28, 163, 163, 255), (191, 255, 248, 255)
        c_wd, c_gd, c_bk = (102, 66, 25, 255), (54, 34, 13, 255), (0, 0, 0, 255)
        img.putpixel((13, 2), c_hi)
        img.putpixel((14, 1), c_bk)
        for d in range(7):
            img.putpixel((12 - d, 3 + d), c_hi)
            img.putpixel((13 - d, 4 + d), c_dm)
            img.putpixel((14 - d, 5 + d), c_dd)
        img.putpixel((6, 11), c_gd); img.putpixel((5, 10), c_gd); img.putpixel((7, 9), c_gd); img.putpixel((8, 10), c_gd)
        img.putpixel((4, 11), c_bk); img.putpixel((4, 12), c_wd); img.putpixel((3, 13), c_wd)
        img.putpixel((2, 14), c_gd); img.putpixel((1, 15), c_bk)
    elif name == "item_diamond_pickaxe":
        c_dm, c_dd, c_wd, c_bk = (43, 235, 235, 255), (28, 163, 163, 255), (102, 66, 25, 255), (0, 0, 0, 255)
        for a in range(5):
            img.putpixel((10 + a, 2 + (a - 2 if a > 2 else 0)), c_dm)
            img.putpixel((5 - a, 7 + (a - 2 if a > 2 else 0)), c_dd)
        for h in range(9):
            img.putpixel((10 - h, 6 + h), c_wd)
        img.putpixel((1, 15), c_bk)
    elif name == "item_dirt":
        c_grs, c_drt = (91, 135, 49, 255), (134, 96, 67, 255)
        for r in range(2, 14):
            for c in range(2, 14):
                img.putpixel((c, r), c_grs if r < 6 else c_drt)
    elif name == "item_stone":
        c_st = (125, 125, 125, 255)
        for r in range(2, 14):
            for c in range(2, 14):
                img.putpixel((c, r), c_st)
    elif name == "item_tnt":
        c_red, c_wht = (219, 51, 31, 255), (240, 240, 240, 255)
        for r in range(2, 14):
            for c in range(2, 14):
                img.putpixel((c, r), c_wht if 6 <= r <= 9 else c_red)
    elif name == "item_golden_apple":
        c_gld, c_stm = (255, 215, 0, 255), (100, 60, 20, 255)
        img.putpixel((8, 2), c_stm); img.putpixel((9, 1), c_stm)
        for y in range(4, 14):
            for x in range(4, 14):
                if (x - 8)**2 + (y - 8)**2 <= 25:
                    img.putpixel((x, y), c_gld)
    elif name == "item_bow":
        c_wd, c_st = (139, 90, 43, 255), (230, 230, 230, 255)
        for b in range(9):
            img.putpixel((12 - abs(b - 4), 3 + b), c_wd)
            img.putpixel((8, 3 + b), c_st)
    elif name == "item_elytra":
        c_w1, c_w2 = (108, 98, 117, 255), (76, 68, 84, 255)
        for e in range(10):
            img.putpixel((6 - e // 3, 3 + e), c_w1)
            img.putpixel((9 + e // 3, 3 + e), c_w2)
    elif name == "item_totem_of_undying":
        c_gld, c_emr = (255, 199, 0, 255), (64, 219, 112, 255)
        for y in range(3, 14):
            for x in range(5, 11):
                img.putpixel((x, y), c_gld)
        img.putpixel((6, 5), c_emr); img.putpixel((9, 5), c_emr)
    elif name.startswith("block_") and name in _PLACEHOLDER_ITEM_COLORS:  # a flat coloured square
        img = Image.new("RGBA", (16, 16), _PLACEHOLDER_ITEM_COLORS[name] + (255,))
    elif name in _PLACEHOLDER_ITEM_COLORS:  # a plain coloured disc; the real icon replaces it when a client.jar is given
        col = _PLACEHOLDER_ITEM_COLORS[name] + (255,)
        for y in range(16):
            for x in range(16):
                if (x - 7.5) ** 2 + (y - 7.5) ** 2 <= 36:
                    img.putpixel((x, y), col)

    return img


_JAR_TEXTURES = "assets/minecraft/textures/"

# (key, native size). The order is the packing order and therefore the UV layout.
HUD_SPRITES: list[tuple[str, tuple[int, int]]] = [
    ("crosshair", (15, 15)),
    ("heart_container", (9, 9)),
    ("heart_full", (9, 9)),
    ("heart_half", (9, 9)),
    ("hunger_container", (9, 9)),
    ("hunger_full", (9, 9)),
    ("hunger_half", (9, 9)),
    ("hotbar", (182, 22)),
    ("hotbar_selection", (24, 23)),
    ("item_diamond_sword", (16, 16)),
    ("item_diamond_pickaxe", (16, 16)),
    ("item_dirt", (16, 16)),
    ("item_stone", (16, 16)),
    ("item_tnt", (16, 16)),
    ("item_golden_apple", (16, 16)),
    ("item_bow", (16, 16)),
    ("item_elytra", (16, 16)),
    ("item_totem_of_undying", (16, 16)),
    # appended after the original layout, so every earlier UV stays exactly where it was
    ("heart_absorb_full", (9, 9)),
    ("heart_absorb_half", (9, 9)),
    ("particle_crit", (8, 8)),
    ("particle_damage", (8, 8)),
    ("particle_sweep_0", (32, 32)),
    ("particle_sweep_1", (32, 32)),
    ("particle_sweep_2", (32, 32)),
    ("particle_sweep_3", (32, 32)),
    ("particle_sweep_4", (32, 32)),
    ("particle_sweep_5", (32, 32)),
    ("particle_sweep_6", (32, 32)),
    ("particle_sweep_7", (32, 32)),
    # the inventory screen: the rest of the 16x16 item icons, and the two halves of the 3-row container background
    ("item_arrow", (16, 16)),
    ("item_trident", (16, 16)),
    ("item_flint_and_steel", (16, 16)),
    ("item_ender_pearl", (16, 16)),
    ("item_enchanted_golden_apple", (16, 16)),
    ("item_bread", (16, 16)),
    ("item_cooked_beef", (16, 16)),
    ("item_firework_rocket", (16, 16)),
    ("container_top", (176, 71)),
    ("container_bottom", (176, 96)),
    # the faces of the blocks placed in the world (flat, as in the jar)
    ("block_dirt", (16, 16)),
    ("block_stone", (16, 16)),
    ("block_tnt_top", (16, 16)),
    ("block_tnt_side", (16, 16)),
    ("block_tnt_bottom", (16, 16)),
]

_JAR_HUD_SPRITES = {
    "crosshair": "gui/sprites/hud/crosshair.png",
    "heart_container": "gui/sprites/hud/heart/container.png",
    "heart_full": "gui/sprites/hud/heart/full.png",
    "heart_half": "gui/sprites/hud/heart/half.png",
    "hunger_container": "gui/sprites/hud/food_empty.png",
    "hunger_full": "gui/sprites/hud/food_full.png",
    "hunger_half": "gui/sprites/hud/food_half.png",
    "hotbar": "gui/sprites/hud/hotbar.png",
    "hotbar_selection": "gui/sprites/hud/hotbar_selection.png",
    "item_diamond_sword": "item/diamond_sword.png",
    "item_diamond_pickaxe": "item/diamond_pickaxe.png",
    "item_golden_apple": "item/golden_apple.png",
    "item_bow": "item/bow.png",
    "item_elytra": "item/elytra.png",
    "item_totem_of_undying": "item/totem_of_undying.png",
    "heart_absorb_full": "gui/sprites/hud/heart/absorbing_full.png",
    "heart_absorb_half": "gui/sprites/hud/heart/absorbing_half.png",
    "particle_crit": "particle/critical_hit.png",
    "particle_damage": "particle/damage.png",
    **{f"particle_sweep_{i}": f"particle/sweep_{i}.png" for i in range(8)},
    "item_arrow": "item/arrow.png",
    "item_trident": "item/trident.png",
    "item_flint_and_steel": "item/flint_and_steel.png",
    "item_ender_pearl": "item/ender_pearl.png",
    "item_bread": "item/bread.png",
    "item_cooked_beef": "item/cooked_beef.png",
    "item_firework_rocket": "item/firework_rocket.png",
    "block_dirt": "block/dirt.png",
    "block_stone": "block/stone.png",
    "block_tnt_top": "block/tnt_top.png",
    "block_tnt_side": "block/tnt_side.png",
    "block_tnt_bottom": "block/tnt_bottom.png",
}

# The 3-row container background (gui/container/generic_54.png): its top part is 17 + 3 * 18 rows, its bottom part (the
# player's inventory) starts at row 126, as in Minecraft's ContainerScreen.
_CONTAINER_TEXTURE = "gui/container/generic_54.png"
_CONTAINER_PARTS = {"container_top": (0, 0, 176, 71), "container_bottom": (0, 126, 176, 222)}
_GLINT = (130, 60, 220)  # the enchantment glint, flattened into a tint for the enchanted golden apple

# Block items are drawn by Minecraft as isometric cubes: (top, left/right sides)
_JAR_BLOCK_ITEMS = {
    "item_dirt": ("block/dirt.png", "block/dirt.png"),
    "item_stone": ("block/stone.png", "block/stone.png"),
    "item_tnt": ("block/tnt_top.png", "block/tnt_side.png"),
}
_CUBE_SHADE = {"top": 1.0, "left": 0.78, "right": 0.6}


def _open_jar_png(jar_zip: zipfile.ZipFile, relative: str) -> Image.Image | None:
    full = _JAR_TEXTURES + relative
    if full not in jar_zip.namelist():
        return None
    try:
        with jar_zip.open(full) as zf:
            return Image.open(io.BytesIO(zf.read())).convert("RGBA")
    except Exception:
        return None


def _shade(img: Image.Image, factor: float) -> Image.Image:
    out = img.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            px[x, y] = (int(r * factor), int(g * factor), int(b * factor), a)
    return out


def _tint(img: Image.Image, color: tuple[int, int, int], amount: float) -> Image.Image:
    out = img.copy()
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            if a:
                px[x, y] = tuple(int(c * (1 - amount) + t * amount) for c, t in zip((r, g, b), color)) + (a,)
    return out


def _paste_face(canvas: Image.Image, face: Image.Image, origin: tuple[float, float],
                u_axis: tuple[float, float], v_axis: tuple[float, float]) -> None:
    """Map the face texture's unit square onto the parallelogram origin + u*u_axis + v*v_axis."""
    (ux, uy), (vx, vy) = u_axis, v_axis
    det = ux * vy - uy * vx
    if det == 0:
        return
    size = face.width
    src = face.load()
    dst = canvas.load()
    for y in range(canvas.height):
        for x in range(canvas.width):
            dx, dy = x + 0.5 - origin[0], y + 0.5 - origin[1]
            u = (dx * vy - dy * vx) / det
            v = (ux * dy - uy * dx) / det
            if 0.0 <= u < 1.0 and 0.0 <= v < 1.0:
                dst[x, y] = src[int(u * size), int(v * size)]


def isometric_block_icon(top: Image.Image, side: Image.Image, size: int = 16) -> Image.Image:
    """The 16x16 GUI icon Minecraft shows for a block item: top, left and right faces of a cube."""
    h = size / 2.0
    q = size / 4.0
    icon = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    _paste_face(icon, _shade(top, _CUBE_SHADE["top"]), (h, 0.5), (h - 1, q - 0.5 + 0.5), (-(h - 1), q))
    _paste_face(icon, _shade(side, _CUBE_SHADE["left"]), (1.0, q + 0.5), (h - 1, q), (0.0, size - q - 1.5))
    _paste_face(icon, _shade(side, _CUBE_SHADE["right"]), (h, size / 2.0), (h - 1, -q), (0.0, size - q - 1.5))
    return icon


def _fit_exact(src: Image.Image, size: tuple[int, int]) -> Image.Image:
    """Sprites are used at their native size; anything else is centred (never stretched)."""
    if src.size == size:
        return src
    canvas = Image.new("RGBA", size, (0, 0, 0, 0))
    canvas.paste(src, ((size[0] - src.width) // 2, (size[1] - src.height) // 2))
    return canvas


def _read_real_hud_sprite(jar_zip: zipfile.ZipFile, name: str, size: tuple[int, int]) -> Image.Image | None:
    """Real Minecraft sprite for `name`, or None so the caller falls back to the canonical placeholder."""
    if name in _JAR_BLOCK_ITEMS:
        top_path, side_path = _JAR_BLOCK_ITEMS[name]
        top, side = _open_jar_png(jar_zip, top_path), _open_jar_png(jar_zip, side_path)
        if top is None or side is None:
            return None
        return isometric_block_icon(top, side, size[0])
    if name in _CONTAINER_PARTS:
        src = _open_jar_png(jar_zip, _CONTAINER_TEXTURE)
        return None if src is None else src.crop(_CONTAINER_PARTS[name])
    if name == "item_enchanted_golden_apple":
        apple = _open_jar_png(jar_zip, "item/golden_apple.png")
        return None if apple is None else _tint(_fit_exact(apple, size), _GLINT, 0.35)
    relative = _JAR_HUD_SPRITES.get(name)
    if relative is None:
        return None
    src = _open_jar_png(jar_zip, relative)
    return None if src is None else _fit_exact(src, size)


def build_hud_atlas(client_jar: Path | None = None) -> tuple[Image.Image, dict[str, tuple[float, float, float, float]]]:
    """Build the 512x512 RGBA HUD atlas and return (image, uv_mapping). The layout never depends on
    whether a client.jar was given, so generated UV constants stay valid for either atlas."""
    atlas_size = 512
    pad = 1  # transparent gap between sprites so nearest-neighbour sampling never reads a neighbour
    atlas = Image.new("RGBA", (atlas_size, atlas_size), (0, 0, 0, 0))
    uv_map: dict[str, tuple[float, float, float, float]] = {}

    jar_zip = None
    if client_jar and client_jar.is_file():
        try:
            jar_zip = zipfile.ZipFile(client_jar, "r")
        except Exception:
            jar_zip = None

    x, y, row_h = pad, pad, 0
    for name, (w, h) in HUD_SPRITES:
        if x + w + pad > atlas_size:
            x, y, row_h = pad, y + row_h + pad, 0
        if y + h + pad > atlas_size:
            raise ValueError("HUD sprites do not fit in the atlas")

        sprite = _read_real_hud_sprite(jar_zip, name, (w, h)) if jar_zip else None
        if sprite is None:
            sprite = create_canonical_sprite(name)
        atlas.paste(sprite, (x, y))
        uv_map[name] = (x / atlas_size, y / atlas_size, (x + w) / atlas_size, (y + h) / atlas_size)

        x += w + pad
        row_h = max(row_h, h)

    if jar_zip:
        jar_zip.close()
    return atlas, uv_map


_ITEM_KEYS = [
    "item_diamond_sword", "item_diamond_pickaxe", "item_dirt", "item_stone", "item_tnt",
    "item_golden_apple", "item_bow", "item_elytra", "item_totem_of_undying",
]


def _atlas_png(atlas: Image.Image) -> bytes:
    buf = io.BytesIO()
    atlas.save(buf, format="PNG")
    return buf.getvalue()


def export_hud_atlas_header(atlas: Image.Image, uv_map: dict[str, tuple[float, float, float, float]], output_header: Path) -> None:
    """Write include/mc/hud_atlas.hpp: UV constants plus the placeholder atlas PNG embedded as bytes."""
    png_bytes = _atlas_png(atlas)
    lines = [
        "// Minecraft HUD texture atlas definitions and embedded PNG data.",
        "// Auto-generated by tools/extract_mc_assets.py - shared across all game adapters. DO NOT EDIT.",
        "// The embedded PNG is a placeholder; real sprites are extracted locally from a client.jar.",
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <cstddef>",
        "",
        "namespace mc::hud {",
        "",
        "struct HudUV {",
        "    float u0{0.0f};",
        "    float v0{0.0f};",
        "    float u1{1.0f};",
        "    float v1{1.0f};",
        "};",
        "",
    ]
    for key, (u0, v0, u1, v1) in uv_map.items():
        lines.append(f"constexpr HudUV kUV_{key.upper()} = {{{u0:.6f}f, {v0:.6f}f, {u1:.6f}f, {v1:.6f}f}};")
    lines += ["", "constexpr HudUV kUV_ITEMS[9] = {"]
    lines += [f"    kUV_{k.upper()}," for k in _ITEM_KEYS]
    lines += [
        "};",
        "",
        f"constexpr uint32_t kHudAtlasWidth = {atlas.width};",
        f"constexpr uint32_t kHudAtlasHeight = {atlas.height};",
        f"constexpr size_t kHudAtlasPngSize = {len(png_bytes)};",
        "constexpr uint8_t kHudAtlasPngData[" + str(len(png_bytes)) + "] = { " + ", ".join(f"0x{b:02x}" for b in png_bytes) + " };",
        "",
        "} // namespace mc::hud",
        "",
    ]
    output_header.parent.mkdir(parents=True, exist_ok=True)
    output_header.write_text("\n".join(lines), encoding="utf-8")


def export_adapter_atlas_header(uv_map: dict[str, tuple[float, float, float, float]], output_header: Path, namespace: str) -> None:
    """Write an adapter header that forwards the shared mc::hud atlas names into `namespace`."""
    names = [f"kUV_{k.upper()}" for k in uv_map] + ["kUV_ITEMS", "kHudAtlasWidth", "kHudAtlasHeight", "kHudAtlasPngSize", "kHudAtlasPngData"]
    lines = [
        "// Auto-generated by tools/extract_mc_assets.py - DO NOT EDIT MANUALLY",
        "// Forwards to mc_core unified HUD atlas definitions",
        "#pragma once",
        "",
        '#include "mc/hud_atlas.hpp"',
        "",
        f"namespace {namespace} {{",
        "",
        "using ::mc::hud::HudUV;",
    ]
    lines += [f"using ::mc::hud::{n};" for n in names]
    lines += ["", f"}} // namespace {namespace}", ""]
    output_header.parent.mkdir(parents=True, exist_ok=True)
    output_header.write_text("\n".join(lines), encoding="utf-8")


def export_steve_skin(client_jar: Path, out_dir: Path) -> Path:
    """Copy the real wide-arm Steve skin (64x64) out of a local client.jar as steve.png."""
    entry = _JAR_TEXTURES + "entity/player/wide/steve.png"
    with zipfile.ZipFile(client_jar, "r") as jar:
        if entry not in jar.namelist():
            raise FileNotFoundError(f"{entry} not found in {client_jar}")
        skin = Image.open(io.BytesIO(jar.read(entry))).convert("RGBA")
    if skin.size != (64, 64):
        raise ValueError(f"expected a 64x64 skin, got {skin.size}")
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / "steve.png"
    skin.save(out)
    return out


def main():
    parser = argparse.ArgumentParser(description="Extract MC Java assets to standard OBJ/PNG models.")
    parser.add_argument("--client-jar", type=Path, help="Path to Minecraft Java client.jar")
    parser.add_argument("--out-dir", type=Path, default=Path("assets/exported"), help="Output directory")
    parser.add_argument("--export-hud-atlas", action="store_true", help="Export mc_hud_atlas.png and UV JSON")
    parser.add_argument("--export-header", type=Path, help="Export the shared C++ HUD atlas header (include/mc/hud_atlas.hpp)")
    parser.add_argument("--export-adapter-header", type=Path, help="Export an adapter header forwarding the shared atlas names")
    parser.add_argument("--adapter-namespace", default="sekiro::hud", help="Namespace for --export-adapter-header")
    parser.add_argument("--export-steve-skin", action="store_true", help="Export the real Steve skin as steve.png (needs --client-jar)")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    if args.export_steve_skin:
        if not args.client_jar:
            parser.error("--export-steve-skin requires --client-jar")
        print(f"Exported Steve skin to {export_steve_skin(args.client_jar, args.out_dir)}")
    if args.export_hud_atlas or args.export_header or args.export_adapter_header:
        atlas_img, uv_map = build_hud_atlas(args.client_jar)
        atlas_path = args.out_dir / "mc_hud_atlas.png"
        atlas_img.save(atlas_path)
        print(f"Exported HUD atlas to {atlas_path} with {len(uv_map)} UV regions.")

        if args.export_header:
            export_hud_atlas_header(atlas_img, uv_map, args.export_header)
            print(f"Exported C++ HUD header to {args.export_header}")
        if args.export_adapter_header:
            export_adapter_atlas_header(uv_map, args.export_adapter_header, args.adapter_namespace)
            print(f"Exported adapter HUD header to {args.export_adapter_header}")

    print(f"MC asset exporter ready. Output: {args.out_dir}")


if __name__ == "__main__":
    main()

