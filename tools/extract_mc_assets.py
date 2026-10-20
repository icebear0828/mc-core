"""Extract Minecraft Java assets into standard OBJ/PNG 3D models for cross-engine importing."""
from __future__ import annotations
import argparse
import io
import json
import math
from pathlib import Path
import shutil
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


def parse_geometry_json(content: str | dict, target_id: str = "") -> dict:
    data = json.loads(content) if isinstance(content, str) else content
    if not isinstance(data, dict):
        raise ValueError("Invalid geometry JSON: root must be an object")

    geom = None
    identifier = target_id

    # Bedrock 1.12.0+ format: "minecraft:geometry": [...]
    if "minecraft:geometry" in data and isinstance(data["minecraft:geometry"], list):
        geos = data["minecraft:geometry"]
        if target_id:
            for g in geos:
                if g.get("description", {}).get("identifier") == target_id:
                    geom = g
                    break
        if geom is None and geos:
            geom = geos[0]
        if geom is not None:
            desc = geom.get("description", {})
            identifier = desc.get("identifier", target_id or "geometry.unnamed")
            tex_w = desc.get("texture_width", 64)
            tex_h = desc.get("texture_height", 64)
            raw_bones = geom.get("bones", [])
        else:
            raise ValueError(f"Geometry '{target_id}' not found in minecraft:geometry")

    else:
        # Bedrock 1.8.0 format: "geometry.<id>": { "bones": ... }
        geo_keys = [k for k in data.keys() if k.startswith("geometry.")]
        if target_id and target_id in data:
            geo_key = target_id
        elif geo_keys:
            geo_key = geo_keys[0]
        else:
            raise ValueError("No valid geometry found in JSON")
        geom = data[geo_key]
        identifier = geo_key
        tex_w = geom.get("texturewidth", geom.get("texture_width", 64))
        tex_h = geom.get("textureheight", geom.get("texture_height", 64))
        raw_bones = geom.get("bones", [])

    parsed_bones = []
    for b in raw_bones:
        b_name = b.get("name", "")
        b_parent = b.get("parent")
        b_pivot = tuple(b.get("pivot", [0, 0, 0]))
        b_rot = tuple(b.get("rotation", [0, 0, 0]))
        cubes = []
        for c in b.get("cubes", []):
            origin = tuple(c.get("origin", [0, 0, 0]))
            size = tuple(c.get("size", [0, 0, 0]))
            uv = tuple(c.get("uv", [0, 0]))
            inflate = float(c.get("inflate", 0.0))
            mirror = bool(c.get("mirror", b.get("mirror", False)))
            cubes.append({
                "origin": origin,
                "size": size,
                "uv": uv,
                "inflate": inflate,
                "mirror": mirror,
            })
        parsed_bones.append({
            "name": b_name,
            "parent": b_parent,
            "pivot": b_pivot,
            "rotation": b_rot,
            "cubes": cubes,
        })

    return {
        "identifier": identifier,
        "texture_width": tex_w,
        "texture_height": tex_h,
        "bones": parsed_bones,
    }


def _cube_faces_bedrock(origin: tuple[float, float, float], size: tuple[float, float, float],
                        uv: tuple[float, float], inflate: float = 0.0, mirror: bool = False) -> list:
    x0, y0, z0 = origin
    w, h, d = size
    x_min, y_min, z_min = x0 - inflate, y0 - inflate, z0 - inflate
    x_max, y_max, z_max = x0 + w + inflate, y0 + h + inflate, z0 + d + inflate
    u, v = uv

    # Standard box unwrapping:
    # Top (+Y): u+d, v, size w, d
    # Bottom (-Y): u+d+w, v, size w, d
    # North (front, -Z): u+d, v+d, size w, h
    # South (back, +Z): u+d+w+d, v+d, size w, h
    # West (-X): u, v+d, size d, h
    # East (+X): u+d+w, v+d, size d, h
    # Mirror swaps West and East, or flips U.
    u_west = u + d + w if mirror else u
    u_east = u if mirror else u + d + w

    faces = [
        # North / Front (-Z)
        ([(x_min, y_min, z_min), (x_max, y_min, z_min), (x_max, y_max, z_min), (x_min, y_max, z_min)],
         (u + d, v + d, w, h)),
        # South / Back (+Z)
        ([(x_max, y_min, z_max), (x_min, y_min, z_max), (x_min, y_max, z_max), (x_max, y_max, z_max)],
         (u + d + w + d, v + d, w, h)),
        # West (-X)
        ([(x_min, y_min, z_max), (x_min, y_min, z_min), (x_min, y_max, z_min), (x_min, y_max, z_max)],
         (u_west, v + d, d, h)),
        # East (+X)
        ([(x_max, y_min, z_min), (x_max, y_min, z_max), (x_max, y_max, z_max), (x_max, y_max, z_min)],
         (u_east, v + d, d, h)),
        # Top (+Y)
        ([(x_min, y_max, z_min), (x_max, y_max, z_min), (x_max, y_max, z_max), (x_min, y_max, z_max)],
         (u + d, v, w, d)),
        # Bottom (-Y)
        ([(x_min, y_min, z_max), (x_max, y_min, z_max), (x_max, y_min, z_min), (x_min, y_min, z_min)],
         (u + d + w, v, w, d)),
    ]
    return faces


def build_geometry_mesh(geo: dict, texture: Image.Image | None = None) -> dict[str, dict]:
    tw = float(geo.get("texture_width", 64))
    th = float(geo.get("texture_height", 64))
    bone_meshes = {}

    for bone in geo.get("bones", []):
        b_name = bone["name"]
        positions, normals, uvs, colors, triangles = [], [], [], [], []

        for cube in bone.get("cubes", []):
            faces = _cube_faces_bedrock(
                cube["origin"], cube["size"], cube["uv"],
                inflate=cube.get("inflate", 0.0),
                mirror=cube.get("mirror", False),
            )

            if texture is not None:
                # Per-pixel texture unwrapping
                mesh = mesh_from_faces(faces, texture)
                base = len(positions)
                positions.extend(mesh["positions"])
                normals.extend(mesh["normals"])
                uvs.extend(mesh["uv"])
                colors.extend(mesh["colors"])
                triangles.extend([idx + base for idx in mesh["triangles"]])
            else:
                # Direct geometric quad generation (1 quad per face)
                for verts, (u, v, w, h) in faces:
                    base = len(positions)
                    positions.extend(verts)
                    # UV mapping normalized to [0, 1]
                    uv_quad = [
                        (u / tw, (v + h) / th),
                        ((u + w) / tw, (v + h) / th),
                        ((u + w) / tw, (v + h) / th), # will be overwritten below
                        (u / tw, v / th),
                    ]
                    # Actually standard quad UV: top-left, top-right, bottom-right, bottom-left
                    uv_quad = [
                        (u / tw, (v + h) / th),
                        ((u + w) / tw, (v + h) / th),
                        ((u + w) / tw, v / th),
                        (u / tw, v / th),
                    ]
                    uvs.extend(uv_quad)
                    colors.extend([[1.0, 1.0, 1.0, 1.0]] * 4)

                    # Compute face normal
                    a = [verts[1][j] - verts[0][j] for j in range(3)]
                    b = [verts[3][j] - verts[0][j] for j in range(3)]
                    n = [
                        a[1] * b[2] - a[2] * b[1],
                        a[2] * b[0] - a[0] * b[2],
                        a[0] * b[1] - a[1] * b[0],
                    ]
                    length = math.sqrt(sum(c * c for c in n)) or 1.0
                    norm = [c / length for c in n]
                    normals.extend([norm] * 4)

                    # Two CCW triangles
                    triangles.extend([base, base + 1, base + 2, base, base + 2, base + 3])

        bone_meshes[b_name] = {
            "positions": positions,
            "normals": normals,
            "uv": uvs,
            "colors": colors,
            "triangles": triangles,
        }

    return bone_meshes



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
    "item_cooked_beef": (120, 60, 30), "item_firework_rocket": (200, 40, 40), "item_zombie_spawn_egg": (0, 175, 175),
    "item_creeper_spawn_egg": (13, 167, 0), "item_skeleton_spawn_egg": (193, 193, 193),
    "block_dirt": (134, 96, 67), "block_stone": (125, 125, 125), "block_tnt_top": (160, 80, 70),
    "block_tnt_side": (200, 60, 50), "block_tnt_bottom": (160, 80, 70),
    # Mining loot (issue #81): flat placeholder colours, replaced by the client.jar icons.
    "item_coal": (40, 40, 40), "item_raw_copper": (200, 110, 60), "item_raw_iron": (205, 170, 140),
    "item_redstone": (200, 30, 30), "item_raw_gold": (235, 200, 60), "item_lapis_lazuli": (40, 80, 200),
    "item_amethyst_shard": (160, 100, 210), "item_diamond": (90, 220, 220), "item_emerald": (40, 200, 90),
    "item_netherite_scrap": (110, 75, 65), "item_cobblestone": (120, 120, 120),
    # Pickaxe tiers (issue #81): flat placeholder colours, replaced by the client.jar icons.
    "item_wooden_pickaxe": (140, 100, 50), "item_stone_pickaxe": (130, 130, 130),
    "item_iron_pickaxe": (210, 210, 215), "item_golden_pickaxe": (250, 210, 60),
    "item_netherite_pickaxe": (80, 70, 75),
    "block_cobblestone": (120, 120, 120),
}


def create_canonical_sprite(name: str) -> Image.Image:
    """Generate canonical pixel sprite when extracting without an official client.jar."""
    if name == "entity_shield":
        # Placeholder with the real texture's layout (ShieldModel: plate box at texOffs (0, 0), 12x22x1; handle box at (26, 0), 2x6x6):
        # a riveted front at (1, 1), a plain back at (14, 1), iron edges around them and a dark handle.
        sheet = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
        iron, iron_dk = (150, 150, 155, 255), (95, 95, 100, 255)
        wood, wood_dk, rivet = (140, 95, 50, 255), (112, 74, 36, 255), (200, 200, 205, 255)
        for x in range(0, 27):  # the top and bottom edge strips and the side strips of the plate
            for y in range(0, 24):
                if y in (0, 23) or x in (0, 13, 26):
                    sheet.putpixel((x, y), iron if (x + y) % 2 == 0 else iron_dk)
        for face_x in (1, 14):
            for dx in range(12):
                for dy in range(22):
                    rim = dx in (0, 11) or dy in (0, 21)
                    col = iron if rim else (wood if dx % 3 else wood_dk)
                    if face_x == 1 and not rim and dx in (2, 9) and dy % 4 == 2:
                        col = rivet  # the front has rivets, the back does not
                    sheet.putpixel((face_x + dx, 1 + dy), col)
        for x in range(26, 42):  # the handle's six faces
            for y in range(0, 12):
                sheet.putpixel((x, y), iron_dk if (x + y) % 3 else wood_dk)
        return sheet

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

    if name in ("heart_container_blinking", "heart_full_blinking", "heart_half_blinking"):
        base = create_canonical_sprite({"heart_container_blinking": "heart_container", "heart_full_blinking": "heart_full",
                                        "heart_half_blinking": "heart_half"}[name])
        out = Image.new("RGBA", base.size, (0, 0, 0, 0))
        for y in range(base.height):
            for x in range(base.width):
                r, g, b, a = base.getpixel((x, y))
                if a:  # the same shape in white
                    out.putpixel((x, y), (255, 255, 255, a) if (r, g, b) != (0, 0, 0) else (r, g, b, a))
        return out

    if name in ("xp_bar_background", "xp_bar_progress"):
        col = (50, 50, 50, 255) if name == "xp_bar_background" else (128, 255, 32, 255)
        return Image.new("RGBA", (182, 5), col)

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

    if name == "hotbar_offhand_left":
        img = Image.new("RGBA", (29, 24), (0, 0, 0, 0))
        for x in range(29):
            for y in range(24):
                if x == 0 or x == 28 or y == 0 or y == 23:
                    img.putpixel((x, y), (200, 200, 200, 255))
                else:
                    img.putpixel((x, y), (40, 40, 40, 200))
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
    elif name.startswith("item_bow_pulling_"):  # the bow with an arrow nocked, the string drawn back further at each of the three stages
        stage = int(name[-1])
        c_wd, c_st, c_ar = (139, 90, 43, 255), (230, 230, 230, 255), (200, 200, 200, 255)
        for b in range(9):
            img.putpixel((12 - abs(b - 4), 3 + b), c_wd)
        for b in range(9):  # the string, bent back by the draw
            img.putpixel((8 - min(stage + 1, 3) * (4 - abs(b - 4)) // 4, 3 + b), c_st)
        for x in range(5 - stage, 13):  # the arrow
            img.putpixel((x, 7), c_ar)
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
    elif name == "item_shield":
        # Full 16x16 canonical Minecraft shield with iron rim, wood planks, and iron boss/rivet
        c_ir_hi = (220, 220, 225, 255) # iron highlight
        c_ir_md = (180, 180, 185, 255) # iron midtone
        c_ir_dk = (120, 120, 125, 255) # iron shadow
        c_ir_ol = (60, 60, 65, 255)    # iron outline edge
        c_wd_hi = (170, 120, 70, 255)  # wood light grain
        c_wd_md = (145, 95, 50, 255)   # wood base
        c_wd_dk = (115, 75, 35, 255)   # wood seam

        # Shield shape: row ranges [x_start, x_end] (inclusive)
        shield_rows = {
            1: (3, 12),
            2: (2, 13), 3: (2, 13), 4: (2, 13), 5: (2, 13),
            6: (2, 13), 7: (2, 13), 8: (2, 13),
            9: (3, 12), 10: (3, 12),
            11: (4, 11), 12: (4, 11),
            13: (5, 10),
            14: (6, 9),
        }
        for y, (x_min, x_max) in shield_rows.items():
            for x in range(x_min, x_max + 1):
                is_rim = (y == 1 or y == 14 or x == x_min or x == x_max)
                if is_rim:
                    if y == 1 or x == x_min:
                        col = c_ir_hi if (x + y) % 2 == 0 else c_ir_md
                    elif x == x_max or y >= 13:
                        col = c_ir_dk if (x + y) % 2 == 0 else c_ir_ol
                    else:
                        col = c_ir_md
                elif y in (6, 7) and x in (7, 8):
                    # Central iron boss / rivet
                    col = c_ir_hi if (x == 7 and y == 6) else (c_ir_dk if (x == 8 and y == 7) else c_ir_md)
                else:
                    # Wood planks with vertical grain pattern
                    if x in (4, 8, 11):
                        col = c_wd_dk
                    elif (x + y) % 3 == 0:
                        col = c_wd_hi
                    else:
                        col = c_wd_md
                img.putpixel((x, y), col)
    elif name == "item_diamond_helmet":
        c_dm, c_dd = (43, 235, 235, 255), (28, 163, 163, 255)
        for y in range(4, 13):
            for x in range(3, 13):
                if y <= 8 or x in (3, 4, 11, 12):
                    img.putpixel((x, y), c_dm if (x + y) % 2 == 0 else c_dd)
    elif name == "item_diamond_chestplate":
        c_dm, c_dd = (43, 235, 235, 255), (28, 163, 163, 255)
        for y in range(3, 13):
            for x in range(2, 14):
                if (y >= 6 and 5 <= x <= 10) or (x in (2, 3, 4, 11, 12, 13) and y <= 8) or (y <= 5 and (x <= 4 or x >= 11)):
                    img.putpixel((x, y), c_dm if (x + y) % 2 == 0 else c_dd)
    elif name == "item_diamond_leggings":
        c_dm, c_dd = (43, 235, 235, 255), (28, 163, 163, 255)
        for y in range(3, 14):
            for x in range(3, 13):
                if y <= 6 or x in (3, 4, 5, 10, 11, 12):
                    img.putpixel((x, y), c_dm if (x + y) % 2 == 0 else c_dd)
    elif name == "item_diamond_boots":
        c_dm, c_dd = (43, 235, 235, 255), (28, 163, 163, 255)
        for y in range(7, 13):
            for x in range(2, 14):
                if (x in (2, 3, 4, 5) and (y >= 10 or x <= 4)) or (x in (10, 11, 12, 13) and (y >= 10 or x >= 11)):
                    img.putpixel((x, y), c_dm if (x + y) % 2 == 0 else c_dd)
    elif name.startswith("empty_armor_slot_"):
        c_wm = (140, 140, 140, 120)
        part = name.replace("empty_armor_slot_", "")
        if part == "helmet":
            for y in range(5, 11):
                for x in range(4, 12):
                    if y <= 8 or x in (4, 5, 10, 11): img.putpixel((x, y), c_wm)
        elif part == "chestplate":
            for y in range(4, 12):
                for x in range(3, 13):
                    if (y >= 6 and 5 <= x <= 10) or (y <= 5 and (x <= 5 or x >= 10)): img.putpixel((x, y), c_wm)
        elif part == "leggings":
            for y in range(4, 13):
                for x in range(4, 12):
                    if y <= 7 or x in (4, 5, 10, 11): img.putpixel((x, y), c_wm)
        elif part == "boots":
            for y in range(7, 12):
                for x in range(3, 13):
                    if x in (3, 4, 5, 10, 11, 12): img.putpixel((x, y), c_wm)
    elif name in ("armor_empty", "armor_half", "armor_full"):
        img = Image.new("RGBA", (9, 9), (0, 0, 0, 0))
        c_out = (30, 30, 30, 255)
        c_iron = (230, 230, 230, 255)
        c_iron_d = (160, 160, 160, 255)
        c_bg = (50, 50, 50, 180)
        for y in range(9):
            for x in range(9):
                is_pixel = False
                if y == 0 and (x in (1, 2, 6, 7)): is_pixel = True
                elif y in (1, 2) and (1 <= x <= 7): is_pixel = True
                elif y in (3, 4) and (0 <= x <= 8): is_pixel = True
                elif y in (5, 6) and (1 <= x <= 7): is_pixel = True
                elif y == 7 and (2 <= x <= 6): is_pixel = True
                elif y == 8 and (3 <= x <= 5): is_pixel = True
                if not is_pixel: continue
                is_outline = (y == 0 or y == 8 or x == 0 or x == 8 or
                              (y in (1, 2) and x in (1, 7)) or
                              (y in (3, 4) and x in (0, 8)) or
                              (y in (5, 6) and x in (1, 7)) or
                              (y == 7 and x in (2, 6)) or
                              (y == 8 and x in (3, 5)) or
                              (y == 1 and x in (3, 4, 5)))
                if is_outline:
                    img.putpixel((x, y), c_out)
                else:
                    if name == "armor_empty":
                        img.putpixel((x, y), c_bg)
                    elif name == "armor_full":
                        img.putpixel((x, y), c_iron if (x + y) % 2 == 0 else c_iron_d)
                    elif name == "armor_half":
                        if x <= 4:
                            img.putpixel((x, y), c_iron if (x + y) % 2 == 0 else c_iron_d)
                        else:
                            img.putpixel((x, y), c_bg)
    elif name.startswith("destroy_stage_"):
        stage = int(name.split("_")[-1])
        img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
        target_count = 6 + stage * 12
        coords = list(dict.fromkeys([( (x * 7 + y * 13) % 16, (x * 11 + y * 3) % 16 ) for y in range(16) for x in range(16)]))
        for y in range(16):
            for x in range(16):
                if (x, y) not in coords:
                    coords.append((x, y))
        for x, y in coords[:target_count]:
            img.putpixel((x, y), (50, 50, 50, 255))
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
    # the heart blink (a hit flashes the container and shows the lost hearts white) and the experience bar
    ("heart_container_blinking", (9, 9)),
    ("heart_full_blinking", (9, 9)),
    ("heart_half_blinking", (9, 9)),
    ("xp_bar_background", (182, 5)),
    ("xp_bar_progress", (182, 5)),
    # the spawn egg of the first mob (appended: the sprites above keep their places)
    ("item_zombie_spawn_egg", (16, 16)),
    # the bow as it looks while it is drawn (the arrow nocked, the string drawn back further in each of the three stages)
    ("item_bow_pulling_0", (16, 16)),
    ("item_bow_pulling_1", (16, 16)),
    ("item_bow_pulling_2", (16, 16)),
    # the offhand hotbar slot and the shield item icon
    ("hotbar_offhand_left", (29, 24)),
    ("item_shield", (16, 16)),
    # the whole 64x64 shield entity texture: the 3D shield model (plate 12x22x1 and handle 2x6x6) is mapped onto it like the game does
    ("entity_shield", (64, 64)),
    # diamond armor items and empty armor slot watermarks
    ("item_diamond_helmet", (16, 16)),
    ("item_diamond_chestplate", (16, 16)),
    ("item_diamond_leggings", (16, 16)),
    ("item_diamond_boots", (16, 16)),
    ("empty_armor_slot_helmet", (16, 16)),
    ("empty_armor_slot_chestplate", (16, 16)),
    ("empty_armor_slot_leggings", (16, 16)),
    ("empty_armor_slot_boots", (16, 16)),
    # HUD armor bar sprites
    ("armor_empty", (9, 9)),
    ("armor_half", (9, 9)),
    ("armor_full", (9, 9)),
    # 10 stages of block destruction (cracking) overlay
    *[(f"destroy_stage_{i}", (16, 16)) for i in range(10)],
    # Additional authentic spawn eggs (appended at the end to keep earlier UV coordinates invariant)
    ("item_creeper_spawn_egg", (16, 16)),
    ("item_skeleton_spawn_egg", (16, 16)),
    # Mining loot icons (issue #81), appended so every earlier UV stays exactly where it was
    ("item_coal", (16, 16)),
    ("item_raw_copper", (16, 16)),
    ("item_raw_iron", (16, 16)),
    ("item_redstone", (16, 16)),
    ("item_raw_gold", (16, 16)),
    ("item_lapis_lazuli", (16, 16)),
    ("item_amethyst_shard", (16, 16)),
    ("item_diamond", (16, 16)),
    ("item_emerald", (16, 16)),
    ("item_netherite_scrap", (16, 16)),
    ("item_cobblestone", (16, 16)),
    # Pickaxe tier icons (issue #81), appended so every earlier UV stays exactly where it was
    ("item_wooden_pickaxe", (16, 16)),
    ("item_stone_pickaxe", (16, 16)),
    ("item_iron_pickaxe", (16, 16)),
    ("item_golden_pickaxe", (16, 16)),
    ("item_netherite_pickaxe", (16, 16)),
    # The cobblestone block's own face texture (the item icon is the 3D cube, like dirt/stone)
    ("block_cobblestone", (16, 16)),
]


_JAR_HUD_SPRITES = {
    "item_diamond_helmet": "item/diamond_helmet.png",
    "item_diamond_chestplate": "item/diamond_chestplate.png",
    "item_diamond_leggings": "item/diamond_leggings.png",
    "item_diamond_boots": "item/diamond_boots.png",
    "empty_armor_slot_helmet": "gui/sprites/container/slot/helmet.png",
    "empty_armor_slot_chestplate": "gui/sprites/container/slot/chestplate.png",
    "empty_armor_slot_leggings": "gui/sprites/container/slot/leggings.png",
    "empty_armor_slot_boots": "gui/sprites/container/slot/boots.png",
    "armor_empty": "gui/sprites/hud/armor_empty.png",
    "armor_half": "gui/sprites/hud/armor_half.png",
    "armor_full": "gui/sprites/hud/armor_full.png",
    "entity_shield": "entity/shield_base_nopattern.png",
    "crosshair": "gui/sprites/hud/crosshair.png",
    "heart_container": "gui/sprites/hud/heart/container.png",
    "heart_full": "gui/sprites/hud/heart/full.png",
    "heart_half": "gui/sprites/hud/heart/half.png",
    "hunger_container": "gui/sprites/hud/food_empty.png",
    "hunger_full": "gui/sprites/hud/food_full.png",
    "hunger_half": "gui/sprites/hud/food_half.png",
    "hotbar": "gui/sprites/hud/hotbar.png",
    "hotbar_selection": "gui/sprites/hud/hotbar_selection.png",
    "hotbar_offhand_left": "gui/sprites/hud/hotbar_offhand_left.png",
    "item_diamond_sword": "item/diamond_sword.png",
    "item_diamond_pickaxe": "item/diamond_pickaxe.png",
    "item_golden_apple": "item/golden_apple.png",
    "item_bow": "item/bow.png",
    "item_bow_pulling_0": "item/bow_pulling_0.png",
    "item_bow_pulling_1": "item/bow_pulling_1.png",
    "item_bow_pulling_2": "item/bow_pulling_2.png",
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
    "item_zombie_spawn_egg": "item/zombie_spawn_egg.png",
    "item_creeper_spawn_egg": "item/creeper_spawn_egg.png",
    "item_skeleton_spawn_egg": "item/skeleton_spawn_egg.png",
    "block_dirt": "block/dirt.png",
    "block_stone": "block/stone.png",
    "block_tnt_top": "block/tnt_top.png",
    "block_tnt_side": "block/tnt_side.png",
    "block_tnt_bottom": "block/tnt_bottom.png",
    "heart_container_blinking": "gui/sprites/hud/heart/container_blinking.png",
    "heart_full_blinking": "gui/sprites/hud/heart/full_blinking.png",
    "heart_half_blinking": "gui/sprites/hud/heart/half_blinking.png",
    "xp_bar_background": "gui/sprites/hud/experience_bar_background.png",
    "xp_bar_progress": "gui/sprites/hud/experience_bar_progress.png",
    **{f"destroy_stage_{i}": f"block/destroy_stage_{i}.png" for i in range(10)},
    # Mining loot icons (issue #81)
    "item_coal": "item/coal.png",
    "item_raw_copper": "item/raw_copper.png",
    "item_raw_iron": "item/raw_iron.png",
    "item_redstone": "item/redstone.png",
    "item_raw_gold": "item/raw_gold.png",
    "item_lapis_lazuli": "item/lapis_lazuli.png",
    "item_amethyst_shard": "item/amethyst_shard.png",
    "item_diamond": "item/diamond.png",
    "item_emerald": "item/emerald.png",
    "item_netherite_scrap": "item/netherite_scrap.png",
    "item_cobblestone": "block/cobblestone.png",
    # Pickaxe tiers (issue #81)
    "item_wooden_pickaxe": "item/wooden_pickaxe.png",
    "item_stone_pickaxe": "item/stone_pickaxe.png",
    "item_iron_pickaxe": "item/iron_pickaxe.png",
    "item_golden_pickaxe": "item/golden_pickaxe.png",
    "item_netherite_pickaxe": "item/netherite_pickaxe.png",
    "block_cobblestone": "block/cobblestone.png",
}


# The 3-row container background (gui/container/generic_54.png): its top part is 17 + 3 * 18 rows, its bottom part (the
# player's inventory) starts at row 126, as in Minecraft's ContainerScreen.
_CONTAINER_TEXTURE = "gui/container/generic_54.png"
_CONTAINER_PARTS = {"container_top": (0, 0, 176, 71), "container_bottom": (0, 126, 176, 222)}
# Spawn eggs are a grey egg and grey spots tinted per mob (the item model's tint): primary colour for the egg, secondary for the spots.
_SPAWN_EGG_COLOURS = {
    "item_zombie_spawn_egg": ((0x00, 0xAF, 0xAF), (0x79, 0x9C, 0x65)),
    "item_creeper_spawn_egg": ((0x0D, 0xA7, 0x00), (0x00, 0x00, 0x00)),
    "item_skeleton_spawn_egg": ((0xC1, 0xC1, 0xC1), (0x49, 0x49, 0x49)),
}
_GLINT = (130, 60, 220)  # the enchantment glint, flattened into a tint for the enchanted golden apple

# Block items are drawn by Minecraft as isometric cubes: (top, left/right sides)
_JAR_BLOCK_ITEMS = {
    "item_dirt": ("block/dirt.png", "block/dirt.png"),
    "item_stone": ("block/stone.png", "block/stone.png"),
    "item_tnt": ("block/tnt_top.png", "block/tnt_side.png"),
    "item_cobblestone": ("block/cobblestone.png", "block/cobblestone.png"),
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


def _multiply_tint(img: Image.Image, color: tuple[int, int, int]) -> Image.Image:
    """Colour * texel, per channel (how Minecraft tints a grey layer); alpha is kept."""
    out = img.convert("RGBA")
    px = out.load()
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            if a:
                px[x, y] = (r * color[0] // 255, g * color[1] // 255, b * color[2] // 255, a)
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
    if name in _SPAWN_EGG_COLOURS:
        own = _JAR_HUD_SPRITES.get(name)
        finished = _open_jar_png(jar_zip, own) if own else None
        if finished is not None:  # newer jars: the finished texture of the mob's egg
            return _fit_exact(finished, size)
        base, spots = _open_jar_png(jar_zip, "item/spawn_egg.png"), _open_jar_png(jar_zip, "item/spawn_egg_overlay.png")
        if base is None or spots is None:
            return None
        primary, secondary = _SPAWN_EGG_COLOURS[name]
        egg = _multiply_tint(_fit_exact(base, size), primary)
        egg.alpha_composite(_multiply_tint(_fit_exact(spots, size), secondary))
        return egg
    if name == "item_shield":
        # 1.21.8 has no item/shield.png (the inventory icon is the 3D model): use the front face of the plate in the entity texture.
        plate = _open_jar_png(jar_zip, "entity/shield_base_nopattern.png")
        if plate is None or plate.size != (64, 64):
            return None
        front = plate.crop((1, 1, 13, 23))  # 12x22, the face with the rivets
        scaled = front.resize((9, size[1]), Image.BOX)
        icon = Image.new("RGBA", size, (0, 0, 0, 0))
        icon.paste(scaled, ((size[0] - scaled.width) // 2, 0))
        return icon
    if name.startswith("destroy_stage_"):
        relative = _JAR_HUD_SPRITES.get(name)
        if relative is None:
            return None
        src = _open_jar_png(jar_zip, relative)
        if src is None:
            return None
        rgba = src.convert("RGBA")
        px = rgba.load()
        for y in range(rgba.height):
            for x in range(rgba.width):
                r, g, b, a = px[x, y]
                if a <= 1:
                    px[x, y] = (0, 0, 0, 0)
        return _fit_exact(rgba, size)
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


# Mob skins supported for extraction (name -> path inside the jar's textures/)
_MOB_SKINS = {
    "zombie": "entity/zombie/zombie.png",
    "creeper": "entity/creeper/creeper.png",
    "skeleton": "entity/skeleton/skeleton.png",
}


def _limb_rects(u0: int, v0: int, d: int = 4, w: int = 4, h: int = 12) -> dict[str, tuple[int, int, int, int]]:
    """The six faces of a limb's texture block (x, y, width, height), Minecraft's cube UV layout."""
    return {
        "top": (u0 + d, v0, w, d),
        "bottom": (u0 + d + w, v0, w, d),
        "right": (u0, v0 + d, d, h),
        "front": (u0 + d, v0 + d, w, h),
        "left": (u0 + d + w, v0 + d, d, h),
        "back": (u0 + 2 * d + w, v0 + d, w, h),
    }


# (right limb block origin, left limb block origin) in the 64x64 player layout: leg, arm
_LIMB_BLOCKS = (((0, 16), (16, 48)), ((40, 16), (32, 48)))


def mirror_empty_left_limbs(skin: Image.Image) -> Image.Image:
    """Old-layout skins (the zombie) leave the left arm and leg blocks empty because the model mirrors the right limbs.
    The Steve rig reads the modern layout, so fill each empty left block with the mirrored right one: every face flipped
    horizontally and the two side faces swapped. A block that already has pixels is left alone."""
    out = skin.copy()
    for (ru, rv), (lu, lv) in _LIMB_BLOCKS:
        left = _limb_rects(lu, lv)
        alpha = out.getchannel("A")
        if any(alpha.crop((x, y, x + w, y + h)).getbbox() is not None for x, y, w, h in left.values()):
            continue  # the block already has pixels (a modern-layout skin)
        right = _limb_rects(ru, rv)
        swap = {"right": "left", "left": "right"}
        for face, (lx, ly, lw, lh) in left.items():
            sx, sy, sw, sh = right[swap.get(face, face)]
            piece = out.crop((sx, sy, sx + sw, sy + sh)).transpose(Image.FLIP_LEFT_RIGHT)
            out.paste(piece, (lx, ly))
    return out


def export_mob_skin(client_jar: Path, out_dir: Path, mob: str) -> Path:
    """Copy a mob's skin out of a local client.jar as <mob>.png."""
    if mob not in _MOB_SKINS:
        raise ValueError(f"unknown mob {mob!r}; available mobs: {sorted(_MOB_SKINS)}")
    entry = _JAR_TEXTURES + _MOB_SKINS[mob]
    with zipfile.ZipFile(client_jar, "r") as jar:
        if entry not in jar.namelist():
            raise FileNotFoundError(f"{entry} not found in {client_jar}")
        skin = Image.open(io.BytesIO(jar.read(entry))).convert("RGBA")
    if skin.size != (64, 64) and skin.size != (64, 32):
        raise ValueError(f"expected a 64x64 or 64x32 skin, got {skin.size}")
    if skin.size == (64, 64):
        skin = mirror_empty_left_limbs(skin)
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{mob}.png"
    skin.save(out)
    return out


def export_entity_models(models_dir: Path, out_dir: Path) -> list[Path]:
    """Copy all entity geometry .geo.json files to out_dir."""
    out_dir.mkdir(parents=True, exist_ok=True)
    copied = []
    candidates = list(models_dir.glob("*.geo.json")) + list(models_dir.glob("entities/*.geo.json"))
    seen = set()
    for src in candidates:
        if src.name in seen:
            continue
        seen.add(src.name)
        dst = out_dir / src.name
        shutil.copy(src, dst)
        copied.append(dst)
    return copied


def main():
    parser = argparse.ArgumentParser(description="Extract MC Java assets to standard OBJ/PNG models.")
    parser.add_argument("--client-jar", type=Path, help="Path to Minecraft Java client.jar")
    parser.add_argument("--out-dir", type=Path, default=Path("assets/exported"), help="Output directory")
    parser.add_argument("--export-hud-atlas", action="store_true", help="Export mc_hud_atlas.png and UV JSON")
    parser.add_argument("--export-header", type=Path, help="Export the shared C++ HUD atlas header (include/mc/hud_atlas.hpp)")
    parser.add_argument("--export-adapter-header", type=Path, help="Export an adapter header forwarding the shared atlas names")
    parser.add_argument("--adapter-namespace", default="sekiro::hud", help="Namespace for --export-adapter-header")
    parser.add_argument("--export-steve-skin", action="store_true", help="Export the real Steve skin as steve.png (needs --client-jar)")
    parser.add_argument("--export-mob-skin", action="append", choices=sorted(_MOB_SKINS), help="Export a mob's skin as <mob>.png (needs --client-jar; repeatable)")
    parser.add_argument("--export-all-mobs", action="store_true", help="Export all supported mob skins (needs --client-jar)")
    parser.add_argument("--export-entity-models", action="store_true", help="Export all entity geometry JSONs (.geo.json) to --out-dir")
    parser.add_argument("--geometry-json", type=Path, help="Path to a Bedrock/Blockbench geometry.json to extract OBJ models from")
    parser.add_argument("--geometry-texture", type=Path, help="Optional texture PNG for --geometry-json")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    if args.export_entity_models:
        root = Path(__file__).resolve().parent.parent
        copied = export_entity_models(root / "assets/source/models", args.out_dir)
        print(f"Exported {len(copied)} entity geometry models to {args.out_dir}")
    if args.geometry_json:
        geo = parse_geometry_json(args.geometry_json.read_text(encoding="utf-8"))
        tex = Image.open(args.geometry_texture).convert("RGBA") if args.geometry_texture else None
        bone_meshes = build_geometry_mesh(geo, tex)
        for bone_name, mesh in bone_meshes.items():
            obj_content = export_obj(f"{geo['identifier']}_{bone_name}", mesh)
            (args.out_dir / f"{bone_name}.obj").write_text(obj_content, encoding="utf-8")
        print(f"Exported {len(bone_meshes)} bone meshes from {args.geometry_json} to {args.out_dir}")
    if args.export_steve_skin:
        if not args.client_jar:
            parser.error("--export-steve-skin requires --client-jar")
        print(f"Exported Steve skin to {export_steve_skin(args.client_jar, args.out_dir)}")
    mobs_to_export = list(args.export_mob_skin or [])
    if args.export_all_mobs:
        mobs_to_export = sorted(_MOB_SKINS.keys())
    for mob in mobs_to_export:
        if not args.client_jar:
            parser.error("--export-mob-skin / --export-all-mobs requires --client-jar")
        print(f"Exported {mob} skin to {export_mob_skin(args.client_jar, args.out_dir, mob)}")
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

