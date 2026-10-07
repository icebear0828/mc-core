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


def create_canonical_sprite(name: str) -> Image.Image:
    """Generate canonical pixel sprite when extracting without an official client.jar."""
    if name == "crosshair":
        img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
        # Black outline + white cross in center
        for i in range(16):
            if 6 <= i <= 9:
                img.putpixel((7, i), (255, 255, 255, 220))
                img.putpixel((8, i), (255, 255, 255, 220))
                img.putpixel((i, 7), (255, 255, 255, 220))
                img.putpixel((i, 8), (255, 255, 255, 220))
        return img

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

    if name == "hotbar_slot":
        img = Image.new("RGBA", (24, 24), (45, 45, 45, 220))
        # Bevel
        c_dark = (55, 55, 55, 255)
        c_lite = (240, 240, 240, 255)
        for i in range(24):
            img.putpixel((i, 0), c_dark)
            img.putpixel((0, i), c_dark)
            img.putpixel((i, 23), c_lite)
            img.putpixel((23, i), c_lite)
        return img

    if name == "hotbar_cursor":
        img = Image.new("RGBA", (24, 24), (0, 0, 0, 0))
        c_wht = (255, 255, 255, 255)
        c_blk = (0, 0, 0, 255)
        for i in range(24):
            img.putpixel((i, 0), c_wht)
            img.putpixel((i, 1), c_wht)
            img.putpixel((0, i), c_wht)
            img.putpixel((1, i), c_wht)
            img.putpixel((i, 22), c_wht)
            img.putpixel((i, 23), c_wht)
            img.putpixel((22, i), c_wht)
            img.putpixel((23, i), c_wht)
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

    return img


_JAR_TEXTURES = "assets/minecraft/textures/"

_JAR_HUD_SPRITES = {
    "crosshair": "gui/sprites/hud/crosshair.png",
    "heart_full": "gui/sprites/hud/heart/full.png",
    "hunger_full": "gui/sprites/hud/food_full.png",
    "item_diamond_sword": "item/diamond_sword.png",
    "item_diamond_pickaxe": "item/diamond_pickaxe.png",
    "item_dirt": "block/dirt.png",
    "item_stone": "block/stone.png",
    "item_tnt": "block/tnt_side.png",
    "item_golden_apple": "item/golden_apple.png",
    "item_bow": "item/bow.png",
    "item_elytra": "item/elytra.png",
    "item_totem_of_undying": "item/totem_of_undying.png",
}

# The real hotbar is one 182x22 strip: nine 22x22 cells at a 20px pitch (neighbouring cells share a
# 2px border). Cell 1 has both borders and tiles cleanly, so it becomes the single-slot sprite.
_HOTBAR_STRIP = "gui/sprites/hud/hotbar.png"
_HOTBAR_SELECTION = "gui/sprites/hud/hotbar_selection.png"
_HOTBAR_CELL = (20, 0, 42, 22)


def _open_jar_png(jar_zip: zipfile.ZipFile, relative: str) -> Image.Image | None:
    full = _JAR_TEXTURES + relative
    if full not in jar_zip.namelist():
        return None
    try:
        with jar_zip.open(full) as zf:
            return Image.open(io.BytesIO(zf.read())).convert("RGBA")
    except Exception:
        return None


def _fit_sprite(src: Image.Image, size: tuple[int, int]) -> Image.Image:
    """Smaller sprites are pasted unscaled and centred (no resampling artefacts); others are resized."""
    if src.size == size:
        return src
    if src.width <= size[0] and src.height <= size[1]:
        canvas = Image.new("RGBA", size, (0, 0, 0, 0))
        canvas.paste(src, ((size[0] - src.width) // 2, (size[1] - src.height) // 2))
        return canvas
    return src.resize(size, Image.Resampling.NEAREST)


def _read_real_hud_sprite(jar_zip: zipfile.ZipFile, name: str, size: tuple[int, int]) -> Image.Image | None:
    """Real Minecraft sprite for `name`, or None so the caller falls back to the canonical placeholder."""
    if name == "hotbar_slot":
        strip = _open_jar_png(jar_zip, _HOTBAR_STRIP)
        if strip is None or strip.width < _HOTBAR_CELL[2] or strip.height < _HOTBAR_CELL[3]:
            return None
        return _fit_sprite(strip.crop(_HOTBAR_CELL), size)
    if name == "hotbar_cursor":
        selection = _open_jar_png(jar_zip, _HOTBAR_SELECTION)
        if selection is None:
            return None
        canvas = Image.new("RGBA", size, (0, 0, 0, 0))
        canvas.paste(selection, (0, 0))
        return canvas
    relative = _JAR_HUD_SPRITES.get(name)
    if relative is None:
        return None
    src = _open_jar_png(jar_zip, relative)
    return None if src is None else _fit_sprite(src, size)


def build_hud_atlas(client_jar: Path | None = None) -> tuple[Image.Image, dict[str, tuple[float, float, float, float]]]:
    """Build unified 256x256 RGBA HUD texture atlas and return (atlas_image, uv_mapping)."""
    atlas_size = 256
    atlas = Image.new("RGBA", (atlas_size, atlas_size), (0, 0, 0, 0))
    uv_map: dict[str, tuple[float, float, float, float]] = {}

    sprites_to_pack = [
        ("crosshair", (16, 16)),
        ("heart_full", (9, 9)),
        ("hunger_full", (9, 9)),
        ("hotbar_slot", (24, 24)),
        ("hotbar_cursor", (24, 24)),
        ("item_diamond_sword", (16, 16)),
        ("item_diamond_pickaxe", (16, 16)),
        ("item_dirt", (16, 16)),
        ("item_stone", (16, 16)),
        ("item_tnt", (16, 16)),
        ("item_golden_apple", (16, 16)),
        ("item_bow", (16, 16)),
        ("item_elytra", (16, 16)),
        ("item_totem_of_undying", (16, 16)),
    ]

    curr_x, curr_y = 2, 2
    row_height = 0

    jar_zip = None
    if client_jar and client_jar.is_file():
        try:
            jar_zip = zipfile.ZipFile(client_jar, "r")
        except Exception:
            jar_zip = None

    for name, (w, h) in sprites_to_pack:
        if curr_x + w + 2 > atlas_size:
            curr_x = 2
            curr_y += row_height + 2
            row_height = 0

        sprite_img = None
        # Try reading from jar if possible
        if jar_zip:
            sprite_img = _read_real_hud_sprite(jar_zip, name, (w, h))

        if sprite_img is None:
            sprite_img = create_canonical_sprite(name)

        atlas.paste(sprite_img, (curr_x, curr_y), sprite_img)

        u0 = curr_x / float(atlas_size)
        v0 = curr_y / float(atlas_size)
        u1 = (curr_x + w) / float(atlas_size)
        v1 = (curr_y + h) / float(atlas_size)
        uv_map[name] = (u0, v0, u1, v1)

        curr_x += w + 2
        row_height = max(row_height, h)

    if jar_zip:
        jar_zip.close()

    return atlas, uv_map


def export_hud_atlas_header(atlas: Image.Image, uv_map: dict[str, tuple[float, float, float, float]], output_header: Path) -> None:
    """Export C++ header containing atlas UV definitions and embedded PNG data."""
    png_io = io.BytesIO()
    atlas.save(png_io, format="PNG")
    png_bytes = png_io.getvalue()

    hex_bytes = ", ".join(f"0x{b:02x}" for b in png_bytes)

    lines = [
        "// Auto-generated by extract_mc_assets.py - DO NOT EDIT MANUALLY",
        "#pragma once",
        "",
        "#include <cstdint>",
        "#include <cstddef>",
        "",
        "namespace sekiro::hud {",
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
        var_name = f"kUV_{key.upper()}"
        lines.append(f"constexpr HudUV {var_name} = {{{u0:.6f}f, {v0:.6f}f, {u1:.6f}f, {v1:.6f}f}};")

    lines.extend([
        "",
        "constexpr HudUV kUV_ITEMS[9] = {",
        "    kUV_ITEM_DIAMOND_SWORD,",
        "    kUV_ITEM_DIAMOND_PICKAXE,",
        "    kUV_ITEM_DIRT,",
        "    kUV_ITEM_STONE,",
        "    kUV_ITEM_TNT,",
        "    kUV_ITEM_GOLDEN_APPLE,",
        "    kUV_ITEM_BOW,",
        "    kUV_ITEM_ELYTRA,",
        "    kUV_ITEM_TOTEM_OF_UNDYING,",
        "};",
        "",
        f"constexpr uint32_t kHudAtlasWidth = {atlas.width};",
        f"constexpr uint32_t kHudAtlasHeight = {atlas.height};",
        f"constexpr size_t kHudAtlasPngSize = {len(png_bytes)};",
        f"constexpr uint8_t kHudAtlasPngData[{len(png_bytes)}] = {{ {hex_bytes} }};",
        "",
        "} // namespace sekiro::hud",
        "",
    ])

    output_header.parent.mkdir(parents=True, exist_ok=True)
    output_header.write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description="Extract MC Java assets to standard OBJ/PNG models.")
    parser.add_argument("--client-jar", type=Path, help="Path to Minecraft Java client.jar")
    parser.add_argument("--out-dir", type=Path, default=Path("assets/exported"), help="Output directory")
    parser.add_argument("--export-hud-atlas", action="store_true", help="Export mc_hud_atlas.png and UV JSON")
    parser.add_argument("--export-header", type=Path, help="Export C++ header file for HUD atlas")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    if args.export_hud_atlas or args.export_header:
        atlas_img, uv_map = build_hud_atlas(args.client_jar)
        atlas_path = args.out_dir / "mc_hud_atlas.png"
        atlas_img.save(atlas_path)
        print(f"Exported HUD atlas to {atlas_path} with {len(uv_map)} UV regions.")

        if args.export_header:
            export_hud_atlas_header(atlas_img, uv_map, args.export_header)
            print(f"Exported C++ HUD header to {args.export_header}")

    print(f"MC asset exporter ready. Output: {args.out_dir}")


if __name__ == "__main__":
    main()

