"""Bedrock 1.8 / 1.12 geometry.json parsing and mesh building."""
from __future__ import annotations

import json
import math

from PIL import Image

from .cuboid import mesh_from_faces


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
                        ((u + w) / tw, (v + h) / th),  # will be overwritten below
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
