"""Cuboid face generation and per-pixel mesh assembly (no PIL image decoding here)."""
from __future__ import annotations

import math

from PIL import Image

from ..core.constants import PARTS


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
                    uvs.append([(u + s * w) / tw, 1.0 - (v + t * h) / th])  # OBJ flip V
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


def build_steve_parts(skin_texture: Image.Image) -> list[dict]:
    parts = []
    for name, pivot, origin, size, uv, inflate in PARTS:
        faces = cuboid(origin, size, uv, inflate=inflate)
        mesh = mesh_from_faces(faces, skin_texture)
        parts.append({"name": name, "pivot": pivot, "mesh": mesh})
    return parts
