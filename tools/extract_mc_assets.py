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
    half = size_cm * 0.5
    # Standard 1x1x1m cube centered at origin
    faces = cuboid((-half, -half, -half), (size_cm, size_cm, size_cm), (0, 0), inflate=0.0)
    return mesh_from_faces(faces, texture)


def main():
    parser = argparse.ArgumentParser(description="Extract MC Java assets to standard OBJ/PNG models.")
    parser.add_argument("--client-jar", type=Path, help="Path to Minecraft Java client.jar")
    parser.add_argument("--out-dir", type=Path, default=Path("assets/exported"), help="Output directory")
    args = parser.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    print(f"MC asset exporter ready. Output: {args.out_dir}")


if __name__ == "__main__":
    main()
